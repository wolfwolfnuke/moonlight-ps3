#ifndef NET_REASSEMBLY_H
#define NET_REASSEMBLY_H

#include <stddef.h>
#include <stdint.h>

/* Called once a complete frame has been reassembled.
 * pts is the frame timestamp if carried in the datagram header (else 0). */
typedef void (*reasm_frame_cb)(void *ctx, const uint8_t *frame,
                               size_t len, int64_t pts);

typedef struct reasm reasm_t;

reasm_t *reasm_create(void);
void reasm_destroy(reasm_t *r);
void reasm_set_callback(reasm_t *r, reasm_frame_cb cb, void *ctx);

/* Feed one received UDP datagram. GameStream can split a single frame across
 * several datagrams; this buffers fragments and invokes the callback when a
 * full frame is available.
 *
 * TODO: parse the real GameStream fragment header (frame index, fragment
 * offset, last-fragment flag) and reassemble across datagrams. For now each
 * datagram is treated as one complete frame. */
void reasm_feed(reasm_t *r, const uint8_t *data, size_t len);

#endif /* NET_REASSEMBLY_H */
