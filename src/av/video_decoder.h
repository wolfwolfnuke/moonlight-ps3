#ifndef AV_VIDEO_DECODER_H
#define AV_VIDEO_DECODER_H

#include <stdint.h>
#include <stddef.h>

/* A decoded video frame in YUV420 planar layout. The pointers are owned by the
 * decoder and only valid until the next pump() call. */
typedef struct video_frame {
    uint8_t *y, *u, *v;
    int width, height;
    int linesize[3];
    int64_t pts;
} video_frame_t;

typedef struct video_decoder video_decoder_t;
struct video_decoder {
    /* Feed one Annex-B H.264 chunk (a single frame / slice batch). */
    int (*submit)(video_decoder_t *vd, const uint8_t *data, size_t len, int64_t pts);
    /* Pull one decoded frame; returns 1 if a frame is available, 0 if not. */
    int (*pump)(video_decoder_t *vd, video_frame_t *out);
    void (*destroy)(video_decoder_t *vd);
    void *priv;
};

/* Create an H.264 software decoder. Returns NULL if decoding is unavailable
 * (e.g. FFmpeg not built). */
video_decoder_t *video_decoder_create_h264(void);

#endif /* AV_VIDEO_DECODER_H */
