#include "net/http.h"
#include "sock.h"
#include "common/crypto.h"
#include "common/hoststore.h"
#include "common/log.h"

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* NOTE: for the M2 scaffold this client does NOT verify the server
 * certificate (MBEDTLS_SSL_VERIFY_NONE). GameStream hosts use self-signed
 * certs; proper pinning/verification must be added in the pairing module. */

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int n = net_send(fd, buf, (int)len);
    if (n < 0)
        return -1;
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int n = net_recv(fd, buf, (int)len);
    if (n < 0)
        return -1;
    return n; /* 0 == peer closed (EOF) */
}

static int parse_url(const char *url, char *host, int hostlen,
                     int *port, char *path, int pathlen)
{
    if (strncmp(url, "https://", 8) != 0)
        return -1;
    const char *p = url + 8;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    int hostend;
    if (colon && (!slash || colon < slash)) {
        int n = (int)(colon - p);
        if (n >= hostlen) return -1;
        memcpy(host, p, n); host[n] = 0;
        *port = atoi(colon + 1);
        p = colon + 1;
        /* skip port digits */
        while (*p >= '0' && *p <= '9') p++;
    } else {
        hostend = slash ? (int)(slash - p) : (int)strlen(p);
        if (hostend >= hostlen) return -1;
        memcpy(host, p, hostend); host[hostend] = 0;
        *port = 47989; /* GameStream HTTPS default */
        p = slash ? slash : p + hostend;
    }
    if (slash) {
        int n = (int)strlen(slash);
        if (n >= pathlen) return -1;
        memcpy(path, slash, n + 1);
    } else {
        path[0] = '/'; path[1] = 0;
    }
    return 0;
}

static int tls_connect(int fd, const char *hostname, const paired_host_t *host,
                       mbedtls_ssl_context *ssl, mbedtls_ssl_config *conf,
                       mbedtls_x509_crt *cli_cert, mbedtls_pk_context *cli_key)
{
    mbedtls_ssl_config_init(conf);
    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, crypto_get_drbg());
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);

    if (host && host->paired && host->cert_pem[0] && host->key_pem[0]) {
        mbedtls_x509_crt_init(cli_cert);
        mbedtls_pk_init(cli_key);
        if (            mbedtls_x509_crt_parse(cli_cert,
                (const unsigned char *)host->cert_pem,
                strlen(host->cert_pem) + 1) == 0 &&
            mbedtls_pk_parse_key(cli_key,
                (const unsigned char *)host->key_pem,
                strlen(host->key_pem) + 1, NULL, 0) == 0) {
            mbedtls_ssl_conf_own_cert(conf, cli_cert, cli_key);
        } else {
            LOGW("http: failed to load client cert/key for mutual TLS\n");
            mbedtls_x509_crt_free(cli_cert);
            mbedtls_pk_free(cli_key);
        }
    }

    mbedtls_ssl_init(ssl);
    if (mbedtls_ssl_setup(ssl, conf) != 0)
        return -1;
    mbedtls_ssl_set_bio(ssl, &fd, bio_send, bio_recv, NULL);
    mbedtls_ssl_set_hostname(ssl, hostname);

    if (mbedtls_ssl_handshake(ssl) != 0) {
        LOGE("http: TLS handshake failed\n");
        return -1;
    }
    return 0;
}

static int perform(const char *url, const char *method, const char *payload,
                   const paired_host_t *host, http_response_t *out)
{
    char hostname[128], path[512];
    int port;
    if (parse_url(url, hostname, sizeof(hostname), &port, path, sizeof(path)) != 0) {
        LOGE("http: bad url %s\n", url);
        return -1;
    }

    int fd = net_connect(hostname, port);
    if (fd < 0)
        return -1;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cli_cert;
    mbedtls_pk_context cli_key;
    memset(&cli_key, 0, sizeof(cli_key));

    if (tls_connect(fd, hostname, host, &ssl, &conf, &cli_cert, &cli_key) != 0) {
        net_close(fd);
        return -1;
    }

    char req[1024];
    int hlen = snprintf(req, sizeof(req),
        "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n",
        method, path, hostname);
    if (payload) {
        int plen = (int)strlen(payload);
        hlen += snprintf(req + hlen, sizeof(req) - hlen,
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Content-Length: %d\r\n\r\n", plen);
        if (hlen < (int)sizeof(req))
            memcpy(req + hlen, payload, plen);
        hlen += plen;
    } else {
        hlen += snprintf(req + hlen, sizeof(req) - hlen, "\r\n");
    }

    if (mbedtls_ssl_write(&ssl, (const unsigned char *)req, hlen) < 0) {
        LOGE("http: ssl_write failed\n");
        mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        net_close(fd);
        return -1;
    }

    /* Read the whole response (server closes after Connection: close). */
    size_t cap = 8192, used = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) { net_close(fd); return -1; }
    while (1) {
        if (used + 1024 > cap) { cap *= 2; buf = realloc(buf, cap); }
        int n = mbedtls_ssl_read(&ssl, buf + used, 1024);
        if (n <= 0)
            break;
        used += n;
    }

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    net_close(fd);

    /* Split headers / body. */
    char *hdr_end = NULL;
    for (size_t i = 0; i + 4 <= used; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            hdr_end = (char *)(buf + i);
            break;
        }
    }
    int status = 0;
    if (hdr_end) {
        /* status from first line "HTTP/1.x NNN ..." */
        sscanf((char *)buf, "HTTP/%*s %d", &status);
        char *body = hdr_end + 4;
        size_t body_len = used - (body - (char *)buf);
        out->body = malloc(body_len + 1);
        memcpy(out->body, body, body_len);
        out->body[body_len] = 0;
        out->body_len = body_len;
    } else {
        out->body = malloc(1);
        out->body[0] = 0;
        out->body_len = 0;
    }
    out->status = status;
    free(buf);
    return 0;
}

int http_get(const char *url, const paired_host_t *host, http_response_t *out)
{
    return perform(url, "GET", NULL, host, out);
}

int http_post(const char *url, const paired_host_t *host,
              const char *payload, http_response_t *out)
{
    return perform(url, "POST", payload, host, out);
}

void http_response_free(http_response_t *r)
{
    free(r->body);
    r->body = NULL;
    r->body_len = 0;
    r->status = 0;
}
