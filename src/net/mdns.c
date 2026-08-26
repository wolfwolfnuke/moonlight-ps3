#include "net/mdns.h"
#include "sock.h"
#include "common/log.h"

#include <net/net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define MDNS_GROUP "224.0.0.251"
#define MDNS_PORT  5353

/* One-shot mDNS query for _nvstream._tcp.local, parse the first A + SRV
 * answers we see. This is a best-effort scanner (no caching, no
 * continuation); validate on real HW. */
int mdns_discover(discovered_host_t *out, int max)
{
    int found = 0;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGE("mdns: socket failed\n");
        return 0;
    }

    /* Allow multicast send/receive. */
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    /* Build query: header + QNAME(_nvstream._tcp.local) + PTR/IN. */
    unsigned char q[64];
    int n = 0;
    q[n++] = 0x00; q[n++] = 0x00;        /* id */
    q[n++] = 0x00; q[n++] = 0x00;        /* flags: standard query */
    q[n++] = 0x00; q[n++] = 0x01;        /* qdcount = 1 */
    q[n++] = 0x00; q[n++] = 0x00;
    q[n++] = 0x00; q[n++] = 0x00;
    q[n++] = 0x00; q[n++] = 0x00;
    /* QNAME */
    const char *svc = "_nvstream._tcp.local";
    const char *p = svc;
    while (*p) {
        const char *dot = strchr(p, '.');
        int lab = dot ? (int)(dot - p) : (int)strlen(p);
        q[n++] = (unsigned char)lab;
        memcpy(q + n, p, lab); n += lab;
        p = dot ? dot + 1 : p + lab;
    }
    q[n++] = 0x00;
    q[n++] = 0x00; q[n++] = 0x0c;        /* QTYPE = PTR */
    q[n++] = 0x00; q[n++] = 0x01;        /* QCLASS = IN */

    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port   = htons(MDNS_PORT);
    to.sin_addr.s_addr = inet_addr(MDNS_GROUP);

    if (sendto(fd, (const char *)q, n, 0,
               (struct sockaddr *)&to, sizeof(to)) < 0) {
        LOGW("mdns: sendto failed\n");
        close(fd);
        return 0;
    }

    /* Collect responses for ~1s. */
    unsigned char buf[1500];
    for (int i = 0; i < 20; i++) {
        int r = net_recvfrom(fd, buf, sizeof(buf), 100);
        if (r <= 0)
            continue;
        if (r < 12)
            continue;

        int ancount = (buf[6] << 8) | buf[7];
        int qdcount = (buf[4] << 8) | buf[5];
        int off = 12;
        /* skip questions */
        for (int q = 0; q < qdcount && off < r; q++) {
            while (off < r && buf[off] != 0)
                off += buf[off] + 1;
            off += 1 + 4; /* null + type + class */
        }

        char ip[16] = {0};
        int port = 47989;
        char name[64] = {0};

        for (int a = 0; a < ancount && off + 12 <= r; a++) {
            /* skip (possibly compressed) name */
            while (off < r) {
                if ((buf[off] & 0xC0) == 0xC0) { off += 2; break; }
                if (buf[off] == 0) { off += 1; break; }
                off += buf[off] + 1;
            }
            if (off + 10 > r) break;
            int type = (buf[off] << 8) | buf[off + 1];
            int rdlen = (buf[off + 8] << 8) | buf[off + 9];
            int rd = off + 10;
            off += 10 + rdlen;
            if (off > r) break;

            if (type == 0x0001 && rdlen >= 4 && found < max) { /* A */
                snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                         buf[rd], buf[rd + 1], buf[rd + 2], buf[rd + 3]);
            } else if (type == 0x0021 && rdlen >= 6) {        /* SRV */
                port = (buf[rd + 4] << 8) | buf[rd + 5];
            } else if (type == 0x000c && name[0] == 0) {      /* PTR */
                /* instance name starts after the first label(s) */
                int o = rd;
                if (o < r && buf[o] != 0 && (buf[o] & 0xC0) != 0xC0) {
                    int l = buf[o];
                    if (o + l + 1 < (int)sizeof(name))
                        memcpy(name, buf + o + 1, l);
                }
            }
        }

        if (ip[0] && found < max) {
            strncpy(out[found].ip, ip, sizeof(out[found].ip) - 1);
            out[found].https_port = port;
            strncpy(out[found].name, name[0] ? name : ip,
                    sizeof(out[found].name) - 1);
            found++;
        }
    }

    close(fd);
    return found;
}
