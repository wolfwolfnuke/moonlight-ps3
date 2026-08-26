#ifndef NET_MDNS_H
#define NET_MDNS_H

#include <stddef.h>

/* GameStream hosts advertise via mDNS (_nvstream._tcp.local) on port 5353.
 * mDNS is best-effort; the UI also supports manual IP entry. */

#define MAX_DISCOVERED 16

typedef struct {
    char name[64];
    char ip[16];
    int  https_port;
} discovered_host_t;

/* Query the local network; blocks ~1s. Returns count found (<= MAX_DISCOVERED). */
int mdns_discover(discovered_host_t *out, int max);

#endif /* NET_MDNS_H */
