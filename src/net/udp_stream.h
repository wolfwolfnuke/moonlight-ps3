#ifndef NET_UDP_STREAM_H
#define NET_UDP_STREAM_H

#include <stddef.h>
#include "net/rtsp.h"

typedef struct {
    int video_fd;
    int audio_fd;
    int control_fd;
} udp_stream_t;

enum { UDP_VIDEO, UDP_AUDIO, UDP_CONTROL };

/* Bind the three local UDP receive sockets from the RTSP session. */
int  udp_open(udp_stream_t *u, const rtsp_session_t *s);
void udp_close(udp_stream_t *u);

/* Receive one datagram on a channel. Returns bytes read (>0), 0 on timeout,
 * <0 on error. GameStream sends whole frames per UDP datagram. */
int  udp_recv(udp_stream_t *u, int which, uint8_t *buf, size_t cap, int ms);

/* AES-128-CBC decrypt a control-channel message using the pairing key/iv.
 * TODO: confirm the exact mode/padding Sunshine uses for control messages. */
int  control_decrypt(const uint8_t *enc, size_t elen,
                     uint8_t *dec, size_t *dlen,
                     const uint8_t key[16], const uint8_t iv[16]);

/* AES-128-CBC encrypt a control message: a random 16-byte IV is generated and
 * prepended to the ciphertext (PKCS#7 padded). Returns total bytes written to
 * out, or -1 on error. out must be at least 16 + ROUNDUP16(plen). */
int  control_encrypt(uint8_t *out, size_t outcap,
                     const uint8_t *plain, size_t plen,
                     const uint8_t key[16]);

/* Send the periodic RI keepalive (and answer RA) on the control channel.
 * TODO: the real message is an AES-encrypted control packet; this is a
 * placeholder send to keep the socket alive. */
int  control_keepalive(int control_fd, const char *host, int control_port);

#endif /* NET_UDP_STREAM_H */
