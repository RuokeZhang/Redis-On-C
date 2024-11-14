#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

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

static int32_t send_req(int fd, const char *text)
{
    // get msg's len
    uint32_t len = (uint32_t)strlen(text);
    if (len > k_max_msg)
    {
        return -1;
    }
    // fill out wbuf. the length takes up 4 bytes.
    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], text, len);
    // send to the sockfd
    return write_all(sockfd, wbuf, len + 4);
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

    // print the msg
    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}

int main()
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

    // Prepare a list of queries to send, loop to send each query using send_req
    const char *quries[3] = {"hello", "ruoke", "zhang"};
    for (size_t i = 0; i < 3; ++i)
    {
        int32_t r = send_req(sockfd, quries[i]);
        if (r < 0)
        {
            msg("send message error");
            close(sockfd);
            return 0;
        }
    }
    // loop to read reponses
    for (size_t i = 0; i < 3; i++)
    {
        int32_t r = read_res(sockfd);
        if (r)
        {
            msg("read message error");
            close(sockfd);
            return 0;
        }
    }
}