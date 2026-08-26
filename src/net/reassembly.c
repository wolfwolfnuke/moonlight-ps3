#include "net/reassembly.h"

#include <stdlib.h>
#include <string.h>

#include "common/log.h"

/* GameStream video/audio use RTP. The video payload is an NV_VIDEO_PACKET
 * (see moonlight-common-c Video.h) after the 12-byte RTP header; it carries
 * frameIndex + SOF/EOF flags used to reassemble a frame across datagrams.
 *
 * Video packet layout:
 *   [RTP header: 12 bytes] [NV_VIDEO_PACKET: 16 bytes] [H.264 NAL data...]
 * The RTP header may carry CSRC (0..15 *4 bytes) and an extension (+4 bytes).
 *
 * Audio packet layout:
 *   [RTP header: 12 bytes] [AAC data...]
 */

#define RTP_HEADER_FIXED 12
#define NV_VIDEO_PACKET_SIZE 16

#define FLAG_SOF 0x4
#define FLAG_EOF 0x2

struct reasm {
    reasm_frame_cb cb;
    void          *ctx;
    int            is_video;

    /* video reassembly state */
    uint32_t  cur_frame;
    int       have_frame;
    uint8_t  *buf;
    size_t    buflen, bufcaps;
};

reasm_t *reasm_create(int is_video)
{
    reasm_t *r = calloc(1, sizeof(*r));
    if (r)
        r->is_video = is_video;
    return r;
}

void reasm_destroy(reasm_t *r)
{
    if (r) {
        free(r->buf);
        free(r);
    }
}

void reasm_set_callback(reasm_t *r, reasm_frame_cb cb, void *ctx)
{
    r->cb  = cb;
    r->ctx = ctx;
}

static int rtp_payload_offset(const uint8_t *data, size_t len)
{
    if (len < RTP_HEADER_FIXED)
        return -1;
    uint8_t b0 = data[0];
    int cc   = b0 & 0x0F;            /* CSRC count */
    int ext  = (b0 & 0x10) ? 4 : 0;  /* extension header */
    return RTP_HEADER_FIXED + cc * 4 + ext;
}

static void emit_video(reasm_t *r)
{
    if (!r->cb || r->buflen == 0)
        return;
    r->cb(r->ctx, r->buf, r->buflen, (int64_t)r->cur_frame);
    r->buflen = 0;
    r->have_frame = 0;
}

void reasm_feed(reasm_t *r, const uint8_t *data, size_t len)
{
    if (!r || !r->cb || len == 0)
        return;

    int off = rtp_payload_offset(data, len);
    if (off < 0 || (size_t)off >= len) {
        LOGW("reasm: datagram too small (%zu)\n", len);
        return;
    }

    if (!r->is_video) {
        /* Audio: emit the RTP-stripped payload as one frame. */
        r->cb(r->ctx, data + off, len - off, 0);
        return;
    }

    if ((size_t)off + NV_VIDEO_PACKET_SIZE > len) {
        LOGW("reasm: video packet missing NV_VIDEO_PACKET header\n");
        return;
    }

    const uint8_t *nv = data + off;
    /* NV_VIDEO_PACKET fields are little-endian on the wire. */
    uint32_t frameIndex = (uint32_t)nv[4] | ((uint32_t)nv[5] << 8) |
                          ((uint32_t)nv[6] << 16) | ((uint32_t)nv[7] << 24);
    uint8_t flags = nv[8];
    const uint8_t *payload = nv + NV_VIDEO_PACKET_SIZE;
    size_t plen = len - off - NV_VIDEO_PACKET_SIZE;

    if (flags & FLAG_SOF) {
        /* start of a new frame: reset the buffer */
        r->buflen = 0;
        r->cur_frame = frameIndex;
        r->have_frame = 1;
    } else if (!r->have_frame) {
        /* continuation without SOF: ignore (we lost the start) */
        return;
    }

    /* append this packet's payload */
    if (r->buflen + plen > r->bufcaps) {
        r->bufcaps = (r->buflen + plen) * 2 + 4096;
        r->buf = realloc(r->buf, r->bufcaps);
        if (!r->buf) {
            r->bufcaps = r->buflen = 0;
            return;
        }
    }
    memcpy(r->buf + r->buflen, payload, plen);
    r->buflen += plen;

    if (flags & FLAG_EOF) {
        emit_video(r);
    }
}
