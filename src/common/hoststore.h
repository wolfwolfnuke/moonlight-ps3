#ifndef HOSTSTORE_H
#define HOSTSTORE_H

#include <stdint.h>

#define HOST_NAME_LEN  64
#define HOST_IP_LEN    16
#define HOST_UUID_LEN  128
#define HOST_CERT_LEN  2048
#define MAX_HOSTS      16

/* A paired GameStream host. The client cert/key (PEM) are generated during
 * pairing and persisted on disk; they are presented for mutual TLS on every
 * subsequent HTTPS request. */
typedef struct {
    char    name[HOST_NAME_LEN];
    char    ip[HOST_IP_LEN];
    char    uuid[HOST_UUID_LEN];
    char    unique_id[17];           /* 8 random bytes, hex (pairing identity) */
    int     httpPort;                /* GameStream HTTP port (default 47989) */
    int     httpsPort;               /* GameStream HTTPS port (default 47984) */
    int     serverMajorVersion;      /* GFE/Sunshine major version */
    uint8_t rikey[16];               /* per-session AES key for control/input */
    uint32_t rikeyid;                /* rikey id sent at launch */
    uint8_t key[16];
    uint8_t iv[16];
    char    cert_pem[HOST_CERT_LEN]; /* client cert (PEM) used for mutual TLS */
    char    key_pem[HOST_CERT_LEN];  /* client private key (PEM) */
    int     paired;
} paired_host_t;

/* Load persisted hosts from disk (creates empty store if none). */
int  hoststore_load(void);
/* Flush in-memory hosts to disk. */
int  hoststore_save(void);

int  hoststore_count(void);
paired_host_t *hoststore_get(int index);
/* Add or update a host by IP. Returns pointer to the stored record. */
paired_host_t *hoststore_add(const char *ip, const char *name);

#endif /* HOSTSTORE_H */
