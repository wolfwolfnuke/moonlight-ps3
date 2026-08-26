#include "av/pipeline.h"

#include <stdlib.h>

#ifdef HAVE_FFMPEG

#include "av/video_decoder.h"
#include "av/audio_decoder.h"
#include "av/audio_playback.h"
#include "av/jitter.h"
#include "render/rsx_renderer.h"

#endif

struct pipeline {
#ifdef HAVE_FFMPEG
    video_decoder_t *vd;
    audio_decoder_t *ad;
    jitter_buf_t    *jb;
    audio_playback_t *ap;
    rsx_renderer_t  *rr;
    int w, h;
#endif
};

pipeline_t *pipeline_create(int width, int height)
{
    pipeline_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
#ifdef HAVE_FFMPEG
    p->w = width;
    p->h = height;
    p->vd = video_decoder_create_h264();
    p->ad = audio_decoder_create_aac();
    p->jb = jitter_buf_create(8);
    p->ap = audio_playback_init(2, 48000);
    p->rr = rsx_renderer_init(width, height);
#else
    (void)width;
    (void)height;
#endif
    return p;
}

void pipeline_submit_video(pipeline_t *p, const uint8_t *data, size_t len, int64_t pts)
{
#ifdef HAVE_FFMPEG
    if (p->vd)
        p->vd->submit(p->vd, data, len, pts);
#else
    (void)p; (void)data; (void)len; (void)pts;
#endif
}

void pipeline_submit_audio(pipeline_t *p, const uint8_t *data, size_t len, int64_t pts)
{
#ifdef HAVE_FFMPEG
    if (p->ad)
        p->ad->submit(p->ad, data, len, pts);
#else
    (void)p; (void)data; (void)len; (void)pts;
#endif
}

/* Hold this many decoded video frames before presenting, to absorb network
 * jitter. Real A/V sync would present based on the audio clock vs. PTS. */
#define PIPELINE_MIN_VIDEO_FRAMES 2

void pipeline_pump(pipeline_t *p)
{
#ifdef HAVE_FFMPEG
    video_frame_t vf;
    if (p->vd)
        while (p->vd->pump(p->vd, &vf))
            jitter_buf_push(p->jb, &vf);

    audio_frame_t af;
    if (p->ad)
        while (p->ad->pump(p->ad, &af))
            if (p->ap)
                audio_playback_submit(p->ap, af.pcm, af.samples, af.channels, af.sample_rate);

    /* Present the oldest decoded frame only after buffering a few, so we can
     * ride out arrival jitter. TODO: drive presentation off the audio clock
     * and use per-frame PTS for true AV sync. */
    if (jitter_buf_size(p->jb) >= PIPELINE_MIN_VIDEO_FRAMES) {
        video_frame_t *disp = jitter_buf_pop(p->jb);
        if (disp) {
            if (p->rr)
                rsx_renderer_present_yuv(p->rr, disp);
            jitter_frame_free(disp);
        }
    }
#else
    (void)p;
#endif
}

void pipeline_destroy(pipeline_t *p)
{
    if (!p)
        return;
#ifdef HAVE_FFMPEG
    if (p->vd) p->vd->destroy(p->vd);
    if (p->ad) p->ad->destroy(p->ad);
    if (p->jb) jitter_buf_destroy(p->jb);
    if (p->ap) audio_playback_destroy(p->ap);
    if (p->rr) rsx_renderer_shutdown(p->rr);
#endif
    free(p);
}
