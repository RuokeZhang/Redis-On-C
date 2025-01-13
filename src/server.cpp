/*
** server.c -- a stream socket server demo
*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <map>
#include "hashtable.h"
#include "zset.h"
#include "common.h"

#define PORT "3490" // the port users will be connecting to

#define BACKLOG 10 // how many pending connections queue will hold

template <class P, class M>
size_t my_offsetof(const M P::*member)
{
    return (size_t) & (reinterpret_cast<P *>(0)->*member);
}

template <class P, class M>
P *my_container_of_impl(M *ptr, const M P::*member)
{
    return (P *)((char *)ptr - my_offsetof(member));
}

#define my_container_of(ptr, type, member) \
    my_container_of_impl(ptr, &type::member)

#include <type_traits>
#define my_typeof(___zarg) std::remove_reference<decltype(___zarg)>::type

static void msg(const char *msg)
{
    perror(msg);
}

enum
{
    STATE_REQ = 0, // reading request
    STATE_RES = 1, // sending responses
    STATE_END = 2, // mark the connection for deletion
};

enum
{
    ERR_UNKNOWN = 1,
    ERR_2BIG = 2,
    ERR_TYPE = 3,
    ERR_ARG = 4,
};

enum
{
    T_STR = 0,
    T_ZSET = 1,
};

static struct
{
    HMap db;
} g_data;

static void out_nil(std::string &out)
{
    // represent the serialized data is a nil
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.size();
    out.append((char *)&len, 4);
    out.append(val);
}
static void out_str(std::string &out, const char *s, size_t size)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)size;
    out.append((char *)&len, 4);
    out.append(s, len);
}

static void out_dbl(std::string &out, double val)
{
    out.push_back(SER_DBL);
    out.append((char *)&val, 8);
}

// TODO: unsafe implementation
static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

static bool str2dbl(const std::string &s, double &out)
{
    char *endp = NULL;
    out = strtod(s.c_str(), &endp);
    return endp == s.c_str() + s.size() && !isnan(out);
}

static bool str2int(const std::string &s, int64_t &out)
{
    char *endp = NULL;
    out = strtoll(s.c_str(), &endp, 10);
    return endp == s.c_str() + s.size();
}

struct Entry
{
    struct HNode node;
    std::string key;
    std::string val;
    uint32_t type = 0;
    ZSet *zset = NULL;
};

static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = my_container_of(lhs, Entry, node);
    struct Entry *re = my_container_of(rhs, Entry, node);
    // compare two pointers, check if they point to the same object
    return le->key == re->key;
}
static void entry_del(Entry *ent)
{
    switch (ent->type)
    {
    case T_ZSET:
        zset_dispose(ent->zset);
        delete ent->zset;
        break;
    }
    delete ent;
}

const size_t k_max_msg = 4096;

struct Conn
{
    int fd = -1;
    uint32_t state = 0;
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// set a fd to non-blocking mode
static void fd_set_nb(int fd)
{
    errno = 0;
    // 获取fd的访问模式和阻塞状态
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        die("fcntl 出错了");
        return;
    }
    // 给它的flag加上非阻塞属性
    flags |= O_NONBLOCK;

    errno = 0;
    // 明确地忽略返回值
    (void)fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        die("fcntl 出错了");
        return;
    }
}
// flush the write buffer, and set state to REQ
static bool try_flush_buffer(Conn *conn)
{
    // send data from write buffer to client's fd
    ssize_t rv = 0;
    do
    {
        size_t remain_bytes = conn->wbuf_size - conn->wbuf_sent;
        rv = send(conn->fd, &conn->wbuf[conn->wbuf_sent], remain_bytes, 0);
    } while (rv < 0 && errno == EINTR);

    // if there's nothing to send
    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }
    // if send error
    if (rv < 0)
    {
        msg("send() error");
        conn->state = STATE_END;
        return false;
    }

    // update wbuf_sent's size
    conn->wbuf_sent += (size_t)rv;

    // if response if fully sent, return false, and change state back
    if (conn->wbuf_sent == conn->wbuf_size)
    {
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        conn->state = STATE_REQ;
        return false;
    }
    return true;
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

static bool cmd_is(const std::string &word, const char *cmd)
{
    return 0 == strcasecmp(word.c_str(), cmd);
}

const size_t k_max_args = 1024;

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    // store the number of parameters in n
    uint32_t n = 0;
    memcpy(&n, &data[0], 4);

    if (n > k_max_args)
    {
        return -1;
    }
    // record current parsing position
    size_t pos = 4;

    // parse parameters in a loop
    while (n--)
    {
        if (pos + 4 > len)
        {
            return -1;
        }
        // get current parameter's length
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + sz > len)
        {
            return -1;
        }
        // convert the parameter from chars to a string, then store it in cmd
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }
    if (pos != len)
    {
        return -1;
    }
    return 0;
}

// HNode *hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
static void do_get(const std::vector<std::string> &cmd, std::string &out)
{
    Entry entry;
    entry.key = std::move(cmd[1]);
    entry.node.hcode = str_hash((uint8_t *)entry.key.data(), entry.key.size());
    // search for the node in the hashmap
    HNode *node = hm_lookup(&g_data.db, &entry.node, &entry_eq);
    if (!node)
    {
        return out_nil(out);
    }

    // if this node exists, we return its value
    const std::string &val = my_container_of(node, Entry, node)->val;

    out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    Entry entry;
    entry.key = std::move(cmd[1]);
    entry.val = std::move(cmd[2]);
    entry.node.hcode = str_hash((uint8_t *)entry.key.data(), entry.key.size());

    // search for the node in the hashmap
    HNode *node = hm_lookup(&g_data.db, &entry.node, &entry_eq);
    // if not exist
    if (!node)
    {
        // entry here is just a local variable. If we insert entry, then it will cause dangling pointer.
        //  Bc h_insert just save a pointer to the HNode, not the data itself
        // so we use new to create a variable in the heap
        Entry *new_entry = new Entry(entry);
        hm_insert(&g_data.db, &new_entry->node);
    }
    // if the key exists, replace its value
    else
    {
        Entry *existing_entry = my_container_of(node, Entry, node);
        existing_entry->val = std::move(cmd[2]);
    }
    return out_nil(out);
}

static void do_del(const std::vector<std::string> &cmd, std::string &out)
{
    Entry entry;
    entry.key = std::move(cmd[1]);
    entry.node.hcode = str_hash((uint8_t *)entry.key.data(), entry.key.size());
    HNode *node = hm_pop(&g_data.db, &entry.node, &entry_eq);
    if (node)
    {
        delete my_container_of(node, Entry, node);
    }
    return out_int(out, node ? 1 : 0);
}

// zadd zset score name
static void do_zadd(std::vector<std::string> &cmd, std::string &out)
{
    double score = 0;
    if (!str2dbl(cmd[2], score))
    {
        return out_err(out, ERR_ARG, "expect fp number");
    }
    // lookup or create the zset
    Entry entry;
    entry.key.swap(cmd[1]);
    entry.node.hcode = str_hash((uint8_t *)entry.key.data(), entry.key.size());
    HNode *hnode = hm_lookup(&g_data.db, &entry.node, &entry_eq);
    Entry *ent = NULL;
    if (!hnode)
    {
        printf("s1\n");
        // if we don't have that zset
        ent = new Entry();
        ent->key.swap(entry.key);
        ent->node.hcode = entry.node.hcode;
        ent->type = T_ZSET;
        ent->zset = new ZSet();
        hm_insert(&g_data.db, &ent->node);
    }
    else
    {
        printf("s2\n");
        ent = my_container_of(hnode, Entry, node);
        if (ent->type != T_ZSET)
        {
            return out_err(out, ERR_TYPE, "expect zset");
        }
    }
    // add or update the tuple  to the zset
    const std::string &name = cmd[3];
    bool added = zset_add(ent->zset, name.data(), name.size(), score);
    return out_int(out, (int64_t)added);
}

/**
 * @brief Iterates through all buckets and nodes in the given hash table and applies
 *        a callback function to each node.
 *
 * @param tab Pointer to the hash table (HTab *) to be scanned.
 * @param f Callback function to be called for each node. The callback takes
 *          two arguments: the node (HNode *) and a user-provided argument (void *).
 * @param arg A user-provided argument passed to the callback function for custom use.
 */
static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg)
{
    if (tab->size == 0)
    {
        return;
    }
    for (size_t i = 0; i < tab->mask + 1; ++i)
    {
        HNode *node = tab->tab[i];
        while (node)
        {
            f(node, arg);
            node = node->next;
        }
    }
}

/**
 * @brief Callback function used during hash table scanning to extract and process
 *        keys from hash table nodes. It appends the key from each node to the provided string.
 *
 * @param node Pointer to the current hash table node (HNode *) being processed.
 * @param arg A pointer to a `std::string` (passed as void *) where the keys will be appended.
 */
static void cb_scan(HNode *node, void *arg)
{
    std::string &out = *(std::string *)arg;
    out_str(out, my_container_of(node, Entry, node)->key);
}

/**
 * @brief Gathers all keys from the global database's hash tables and appends them
 *        to the provided output string as an array.
 *
 * @param cmd Unused vector of strings, typically representing input commands.
 * @param out Reference to a `std::string` where the array of keys will be written.
 */
static void do_keys(std::vector<std::string> cmd, std::string &out)
{
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    h_scan(&g_data.db.ht1, &cb_scan, &out);
    h_scan(&g_data.db.ht2, &cb_scan, &out);
}

// return true if ent is of type ZSet and has name s
static bool expect_zset(std::string &out, std::string &s, Entry **ent)
{
    Entry entry;
    entry.key.swap(s);
    entry.node.hcode = str_hash((uint8_t *)entry.key.data(), entry.key.size());
    HNode *hnode = hm_lookup(&g_data.db, &entry.node, &entry_eq);
    if (!hnode)
    {
        out_nil(out);
        return false;
    }
    *ent = my_container_of(hnode, Entry, node);
    if ((*ent)->type != T_ZSET)
    {
        out_err(out, ERR_TYPE, "expect zset");
        return false;
    }
    return true;
}

// zrem zset name
// remove a tuple from the zset
static void do_zrem(std::vector<std::string> &cmd, std::string &out)
{
    Entry *ent = NULL;
    // get the set that has the name we specified
    if (!expect_zset(out, cmd[1], &ent))
    {
        return;
    }
    // ZNode *zset_pop(ZSet *zset, const char *name, size_t len);
    const std::string &name = cmd[1];
    // pop the tuple from the set
    ZNode *znode = zset_pop(ent->zset, name.data(), name.size());
    if (znode)
    {
        znode_del(znode);
    }
    return out_int(out, znode ? 1 : 0);
}

// zscore zset name
static void do_zscore(std::vector<std::string> &cmd, std::string &out)
{
    Entry *ent = NULL;
    // get the set that has the name we specified
    if (!expect_zset(out, cmd[1], &ent))
    {
        return;
    }
    const std::string &name = cmd[2];
    ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
    return znode ? out_dbl(out, znode->score) : out_nil(out);
}

static void *begin_arr(std::string &out)
{
    out.push_back(SER_ARR);
    out.append("\0\0\0\0", 4);       // 预留4个字节用于稍后填充数组长度
    return (void *)(out.size() - 4); // 返回指向预留位置的指针（或偏移量）
}
static void end_arr(std::string &out, void *ctx, uint32_t n)
{
    size_t pos = (size_t)ctx;
    assert(out[pos - 1] == SER_ARR);
    memcpy(&out[pos], &n, 4);
}
// zquery zset score name offset limit
static void do_zquery(std::vector<std::string> &cmd, std::string &out)
{
    // 1. parse args
    double score = 0;
    if (!str2dbl(cmd[2], score))
    {
        return out_err(out, ERR_ARG, "expect fp number");
    }
    const std::string &name = cmd[3];
    int64_t limit = 0;
    int64_t offset = 0;
    if (!str2int(cmd[4], offset))
    {
        return out_err(out, ERR_ARG, "expect int");
    }
    if (!str2int(cmd[5], limit))
    {
        return out_err(out, ERR_ARG, "expect int");
    }

    // 2. get the zset
    Entry *ent = NULL;
    // get the set that has the name we specified
    // why we need to pass a type **Entry? If we pass *Entry, we're passing the address of that struct.
    // It that case, inside function expect_zset, "ent = my_container_of(hnode, Entry, node);" this step will return a pointer to the Entry we want.
    // but it will only change the value of formal parameter ent.
    if (!expect_zset(out, cmd[1], &ent))
    {
        if (out[0] == SER_NIL)
        {
            out.clear();
            out_arr(out, 0);
        }
        return;
    }

    // 3. look up the tuple in the zset
    if (limit <= 0)
    {
        return out_arr(out, 0);
    }
    // ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len)
    ZNode *znode = zset_query(ent->zset, score, name.data(), name.size());
    znode = znode_offset(znode, offset);

    // output
    void *arr = begin_arr(out);
    uint32_t n = 0;
    while (znode && (int64_t)n < limit)
    {
        out_str(out, znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
        n += 2;
    }
    end_arr(out, arr, n);
}
static void do_request(std::vector<std::string> &cmd, std::string &out)
{
    if (cmd.size() == 1 && cmd_is(cmd[0], "keys"))
    {
        do_keys(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        do_get(cmd, out);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        do_set(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        do_del(cmd, out);
    }
    else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd"))
    {
        do_zadd(cmd, out);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem"))
    {
        do_zrem(cmd, out);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore"))
    {
        do_zscore(cmd, out);
    }
    else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery"))
    {
        do_zquery(cmd, out);
    }
    else
    {
        // cmd is not recognized
        out_err(out, ERR_UNKNOWN, "Unknown cmd");
    }
}

// pipeline. There may be more than one request in the read buffer
// This function takes one request from the read buffer, generates a response, then transits to the STATE_RES state.
static bool try_one_request(Conn *conn)
{
    // if not enough data
    if (conn->rbuf_size < 4)
    {
        return false;
    }
    // get the first request's length
    u_int32_t len;
    memcpy(&len, &conn->rbuf[0], 4);
    // if too long
    if (len > k_max_msg)
    {
        msg("message too long.");
        conn->state = STATE_END;
        return false;
    }
    std::vector<std::string> cmd;
    if (0 != parse_req(&conn->rbuf[4], len, cmd))
    {
        msg("bad req");
        conn->state = STATE_END;
        return false;
    }

    // got one request, generate the reponse
    std::string out;
    do_request(cmd, out);
    if (4 + out.size() > k_max_msg)
    {
        out.clear();
        out_err(out, ERR_2BIG, "response is too big");
    }
    uint32_t wlen = (uint32_t)out.size();
    // the first thing in the write buff is the length of response(EXCEPT FOR ITSELF)
    // put the result's length and result in the write buffer
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], out.data(), out.size());
    conn->wbuf_size = wlen + 4;

    // remove this request from the read buffer
    size_t remain_bytes = conn->rbuf_size - len - 4;
    if (remain_bytes > 0)
    {
        memmove(conn->rbuf, &conn->rbuf[len + 4], remain_bytes);
    }
    conn->rbuf_size = remain_bytes;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}
static bool try_fill_buffer(Conn *conn)
{
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    // number of bytes received
    ssize_t rv = 0;
    do
    {
        // sizeof is a compile-time operator, it will return the size of the total array, i.e. the max array size
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = recv(conn->fd, &conn->rbuf[conn->rbuf_size], cap, 0);
    } while (rv < 0 && errno == EINTR); // The receive was interrupted by delivery of a signal before any data was available

    // in non-blocking mode, if there's nothing to read, the value -1 is returned and errno is set to EAGAIN
    if (rv < 0 && errno == EAGAIN)
    {
        return false;
    }

    // if other error
    if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }

    // EOF met. When a stream socket peer has performed an orderly shutdown
    if (rv == 0)
    {
        if (conn->rbuf_size > 0)
        {
            msg("unexpected EOF");
        }
        else
        {
            // normal EOF
        }
        conn->state = STATE_END;
        return false;
    }
    conn->rbuf_size += rv;

    while (try_one_request(conn))
    {
    }
    return false;
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0);
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    // if fd2conn's size isn't enough, resize it
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

// listening fd try to accept new client connections, then put those connections in fd2conn array
static void accept_new_conn(std::vector<Conn *> &fd2conn, int fd)
{
    socklen_t sin_size;
    struct sockaddr_storage their_addr;
    sin_size = sizeof their_addr;
    int new_fd = accept(fd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1)
    {
        die("accept() error");
    }
    // set the new fd to non-blocking mode
    fd_set_nb(new_fd);
    // create a struct conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    // initialize the connection
    conn->fd = new_fd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    // add this connection to fd2conn
    conn_put(fd2conn, conn);
}
int main(void)
{
    int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    // server调用 getaddrinfo 时，通常会将第一个参数（即主机名）设置为 NULL，配合 AI_PASSIVE 标志。这样找到的是一个通配符0.0.0.0
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }
        // allow the socket to bind in the same IP and port again, even if the prior socket is in TIME_WAIT status
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(sockfd);

    // TODO: implement epoll.
    std::vector<struct pollfd> poll_args;
    while (1)
    {
        // prepare the arguments of the poll()
        poll_args.clear();
        // the listening fd is put in the first position
        struct pollfd pfd = {sockfd, POLLIN, 0};
        poll_args.push_back(pfd);
        // deal with connection fds; Af first, fd2conn is empty
        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            // once meet a connection, add it to the poll array
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            // 添加POLLERR状态，代表同时监听错误状态
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }
        // poll for active fds
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0)
        {
            die("poll");
        }
        // process active fds (except for the listening fd)
        for (size_t i = 1; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    fd2conn[conn->fd] = NULL;
                    // close the fd
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }
        // try to accept new connections if the listening fd is active
        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, sockfd);
        }
    }
    close(sockfd);
    return 0;
}
