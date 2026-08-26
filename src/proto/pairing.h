#ifndef PROTO_PAIRING_H
#define PROTO_PAIRING_H

#include "common/hoststore.h"

/* Implements the GameStream pairing state machine:
 * client cert -> RSA/DH shared secret -> PIN phrase -> derive key/iv.
 * On success the host record in hoststore is updated (key/iv/cert) and saved. */

/* PIN is shown to the user and entered on the host. Returns 0 on success. */
int pair_with_host(paired_host_t *host, const char *pin);

#endif /* PROTO_PAIRING_H */
