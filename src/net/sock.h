#ifndef ML_NET_SOCKET_H
#define ML_NET_SOCKET_H

#include <stddef.h>

/* Thin BSD-socket wrapper over PSL1GHT's network stack (<net/net.h>).
 * PSL1GHT auto-initializes the socket layer on real HW/RPCS3; on a real
 * PS3 you may first need to bring the interface up via netctl. */

int  net_init(void);
int  net_connect(const char *host, int port);
int  net_send(int fd, const void *buf, int len);
int  net_recv(int fd, void *buf, int len);
int  net_recv_timeout(int fd, void *buf, int len, int ms);
void net_close(int fd);

/* UDP helpers (used by mDNS and the stream channels). */
int  net_udp_socket(void);
int  net_bind(int fd, int port);
int  net_sendto(int fd, const void *buf, int len,
                const char *host, int port);
int  net_recvfrom(int fd, void *buf, int len, int ms);

#endif /* NET_SOCKET_H */
