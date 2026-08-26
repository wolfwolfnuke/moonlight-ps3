#include "sock.h"
#include "common/log.h"

#include <net/net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

int net_init(void)
{
    /* PSL1GHT's socket stack initializes itself. On real HW, join a
     * connection first (netctl) before calling this. */
    return 0;
}

int net_connect(const char *host, int port)
{
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr) {
        LOGE("net: dns resolution failed for %s\n", host);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOGE("net: socket() failed\n");
        return -1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((unsigned short)port);
    memcpy(&sin.sin_addr, he->h_addr, (size_t)he->h_length);

    if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        LOGE("net: connect to %s:%d failed\n", host, port);
        close(fd);
        return -1;
    }
    return fd;
}

int net_send(int fd, const void *buf, int len)
{
    int sent = 0;
    while (sent < len) {
        int n = send(fd, (const char *)buf + sent, len - sent, 0);
        if (n <= 0) {
            LOGE("net: send error\n");
            return -1;
        }
        sent += n;
    }
    return sent;
}

int net_recv(int fd, void *buf, int len)
{
    return recv(fd, buf, len, 0);
}

int net_recv_timeout(int fd, void *buf, int len, int ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0)
        return 0;
    return recv(fd, buf, len, 0);
}

void net_close(int fd)
{
    if (fd >= 0)
        close(fd);
}

int net_udp_socket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGE("net: udp socket() failed\n");
        return -1;
    }
    return fd;
}

int net_bind(int fd, int port)
{
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((unsigned short)port);
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        LOGE("net: bind to :%d failed\n", port);
        return -1;
    }
    return 0;
}

int net_sendto(int fd, const void *buf, int len, const char *host, int port)
{
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr)
        return -1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port   = htons((unsigned short)port);
    memcpy(&sin.sin_addr, he->h_addr, (size_t)he->h_length);

    return sendto(fd, (const char *)buf, len, 0,
                  (struct sockaddr *)&sin, sizeof(sin));
}

int net_recvfrom(int fd, void *buf, int len, int ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0)
        return 0;

    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    return recvfrom(fd, buf, len, 0, (struct sockaddr *)&from, &fl);
}
