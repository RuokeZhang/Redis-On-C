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
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <map>
#include "hashtable.h"

#define PORT "3490" // the port users will be connecting to

#define BACKLOG 10 // how many pending connections queue will hold

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
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
};

// the data structure for the key space
static std::map<std::string, std::string> g_map;

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

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    errno = saved_errno;
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

static uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    // if we don't have that key
    if (!g_map.count(cmd[1]))
    {
        return RES_NX;
    }
    std::string &val = g_map[cmd[1]];
    memcpy(res, val.data(), val.size());
    *reslen = (uint32_t)val.size();
    return RES_OK;
}

static uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    g_map[cmd[1]] = cmd[2];
    return RES_OK;
}

static uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res, uint32_t *reslen)
{
    g_map.erase(cmd[1]);
    return RES_OK;
}

static int32_t do_request(const uint8_t *req, uint32_t reqlen, uint32_t *rescode, uint8_t *res, uint32_t *reslen)
{
    std::vector<std::string> cmd;
    // parse the request, store commands in cmd
    int rv = parse_req(req, reqlen, cmd);
    if (rv != 0)
    {
        msg("bad request");
        return (-1);
    }
    if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        *rescode = do_get(cmd, res, reslen);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        *rescode = do_set(cmd, res, reslen);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        *rescode = do_del(cmd, res, reslen);
    }
    else
    {
        *rescode = RES_ERR;
        const char *msg = "unknown cmd";
        strcpy((char *)res, msg);
        *reslen = strlen(msg);
        return 0;
    }
    return 0;
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
    // got one request, generate the reponse
    uint32_t rescode = 0;
    uint32_t wlen = 0;
    // parse the first request in the read buffer, and generate corresponding respond, store them in the write buffer
    int32_t err = do_request(&conn->rbuf[4], len, &rescode, &conn->wbuf[4 + 4], &wlen);
    if (err)
    {
        conn->state = STATE_END;
        return false;
    }
    // whatever the result is, we will return a res code. So the first thing in the write buff is the length of response.
    wlen += 4;
    // put the result's length and result code in the write buffer, right before the actual result content
    memcpy(&conn->wbuf[0], &wlen, 4);
    memcpy(&conn->wbuf[4], &rescode, 4);
    conn->wbuf_size = wlen + 4 + 4;

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
        msg("EOF");
        conn->state = STATE_END;
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
