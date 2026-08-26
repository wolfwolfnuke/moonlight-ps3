#ifndef AV_PIPELINE_H
#define AV_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

/* Media pipeline: decoded video frames pass through a jitter buffer to the
 * renderer; decoded audio is sent to the PS3 audio port. Encoded packets are
 * submitted via the *_submit() calls; pipeline_pump() does decode + present.
 *
 * Without FFmpeg (no HAVE_FFMPEG) every call is a no-op stub. */
typedef struct pipeline pipeline_t;

pipeline_t *pipeline_create(int width, int height);
void pipeline_submit_video(pipeline_t *p, const uint8_t *data, size_t len, int64_t pts);
void pipeline_submit_audio(pipeline_t *p, const uint8_t *data, size_t len, int64_t pts);
void pipeline_pump(pipeline_t *p);
void pipeline_destroy(pipeline_t *p);

#endif /* AV_PIPELINE_H */
