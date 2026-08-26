#ifndef NET_HTTP_H
#define NET_HTTP_H

#include <stddef.h>
#include "common/hoststore.h"

/* HTTPS client used for pairing, serverinfo, applist, launch.
 * Uses mbedTLS with the client cert from hoststore for mutual TLS. */

typedef struct http_response {
    int    status;
    char  *body;
    size_t body_len;
} http_response_t;

/* GET/POST to an absolute https://host:port/path. Returns 0 on success. */
int http_get(const char *url, const paired_host_t *host, http_response_t *out);
int http_post(const char *url, const paired_host_t *host,
              const char *payload, http_response_t *out);

void http_response_free(http_response_t *r);

#endif /* NET_HTTP_H */
