#ifndef NET_REASSEMBLY_H
#define NET_REASSEMBLY_H

#include <stddef.h>
#include <stdint.h>

/* Called once a complete frame has been reassembled.
 * pts is the frame timestamp if known (0 if not carried in the datagram). */
typedef void (*reasm_frame_cb)(void *ctx, const uint8_t *frame,
                               size_t len, int64_t pts);

typedef struct reasm reasm_t;

/* is_video: video frames are reassembled across datagrams by frameIndex
 * (GameStream RTP + NV_VIDEO_PACKET); audio frames are emitted per-datagram
 * after stripping the RTP header. */
reasm_t *reasm_create(int is_video);
void reasm_destroy(reasm_t *r);
void reasm_set_callback(reasm_t *r, reasm_frame_cb cb, void *ctx);

/* Feed one received UDP datagram. Video: buffers fragments and invokes the
 * callback when a full frame (SOF..EOF) is available. Audio: strips the RTP
 * header and emits the payload immediately. */
void reasm_feed(reasm_t *r, const uint8_t *data, size_t len);

#endif /* NET_REASSEMBLY_H */
