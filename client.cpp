#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <vector>
#include <arpa/inet.h>

#define PORT "3490"
#define IP "127.0.0.1"
struct addrinfo hints, *servinfo, *p;
int rv;
int sockfd;
const size_t k_max_msg = 4096;

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static int32_t write_all(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = send(fd, buf, n, 0);
        if (rv == -1)
        {
            printf("%d", errno);
            return -1;
        }

        n -= (size_t)rv;
        // move the buffer point rv bytes ahead
        buf += rv;
    }
    return 0;
}

// send a command, like "set key val".  In this function we just put the command in the wbuf.
// this command will be in this form: "{whole length} 3 3 len 3 len 3 len", when stored in the wbuf
static int32_t send_req(int fd, const std::vector<std::string> &cmd)
{
    // get msg's whole len
    uint32_t len = 4;
    for (const std::string &s : cmd)
    {
        len += 4 + s.size();
    }
    if (len > k_max_msg)
    {
        return -1;
    }

    // fill the whole length in wbuf, except for itself
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    // fill the number of paras
    uint32_t n = cmd.size();
    memcpy(&wbuf[4], &n, 4);
    // fill in the paras and their sizes
    size_t margin = 8;
    for (const std::string &s : cmd)
    {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[margin], &p, 4);
        memcpy(&wbuf[margin + 4], s.data(), s.size());
        margin += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

// aim to read n bytes, if it fails once, try again until read full
// will return 0 if success
static int32_t read_full(int fd, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = recv(fd, buf, n, 0);
        if (rv < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (rv == 0)
        {
            return -2;
        }
        n -= (size_t)rv;
        // move the buffer point rv bytes ahead
        buf += rv;
    }
    return 0;
}

static int32_t read_res(int fd)
{
    // define the read buffer
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    // read the length first
    int32_t err = read_full(fd, rbuf, 4);
    if (err)
    {
        if (err == -1)
        {
            msg("recv() error");
        }
        else if (err == -2)
        {
            msg("EOF");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    // if the message is too long
    if (len > k_max_msg)
    {
        printf("%d\n", len);
        msg("msg too long");
        return -1;
    }

    // read the content
    err = read_full(fd, &rbuf[4], len);
    if (err)
    {
        if (err == -1)
        {
            msg("recv() error");
        }
        else if (err == -2)
        {
            msg("EOF");
        }
        return err;
    }

    // print the result
    if (len < 4)
    {
        msg("bad response");
        return -1;
    }
    // get the rescode
    uint32_t rescode = 0;
    memcpy(&rescode, &rbuf[4], 4);
    printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
    return 0;
}

int main(int argc, char **argv)
{
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // 一般通过 DNS查询，把信息存入servinfo。传入查询条件所在的地址。getaddrinfo的最后一个参数是一个指向指针的指针。
    if ((rv = getaddrinfo(IP, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    // loop through all the results and connect to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1)
        {
            perror("client: socket");
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }
    // store those commands in a vector
    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i)
    {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(sockfd, cmd);
    if (err)
    {
        return 1;
    }
    err = read_res(sockfd);
    if (err)
    {
        return 1;
    }
    close(sockfd);
    return 0;
}