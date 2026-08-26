#include "av/audio_decoder.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_FFMPEG

#include <libavcodec/avcodec.h>

typedef struct {
    AVCodecContext *ctx;
    AVFrame *frame;
    int16_t *pcm;
    size_t pcm_cap; /* samples * channels */
} ffmpeg_ad_t;

static int ad_submit(audio_decoder_t *ad, const uint8_t *data, size_t len, int64_t pts)
{
    ffmpeg_ad_t *f = ad->priv;
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = (uint8_t *)(uintptr_t)data;
    pkt.size = (int)len;
    pkt.pts = pts;
    int r = avcodec_send_packet(f->ctx, &pkt);
    av_packet_unref(&pkt);
    return (r == 0 || r == AVERROR(EAGAIN)) ? 0 : -1;
}

static int ad_pump(audio_decoder_t *ad, audio_frame_t *out)
{
    ffmpeg_ad_t *f = ad->priv;
    int r = avcodec_receive_frame(f->ctx, f->frame);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
        return 0;
    if (r < 0)
        return -1;

    AVFrame *fr = f->frame;
    int n = fr->nb_samples;
    int ch = fr->channels;
    size_t total = (size_t)n * ch;
    if (f->pcm_cap < total) {
        free(f->pcm);
        f->pcm = malloc(total * sizeof(int16_t));
        f->pcm_cap = total;
    }

    /* FFmpeg AAC decoders emit AV_SAMPLE_FMT_FLTP (float planar). Convert to
     * interleaved s16 manually to avoid a swresample dependency. */
    if (fr->format == AV_SAMPLE_FMT_FLTP) {
        for (int c = 0; c < ch; c++) {
            const float *src = (const float *)fr->data[c];
            for (int i = 0; i < n; i++) {
                float v = src[i];
                if (v > 1.0f) v = 1.0f;
                else if (v < -1.0f) v = -1.0f;
                f->pcm[i * ch + c] = (int16_t)(v * 32767.0f);
            }
        }
    } else {
        memcpy(f->pcm, fr->data[0], total * sizeof(int16_t));
    }

    out->pcm = f->pcm;
    out->samples = n;
    out->channels = ch;
    out->sample_rate = fr->sample_rate;
    return 1;
}

static void ad_destroy(audio_decoder_t *ad)
{
    ffmpeg_ad_t *f = ad->priv;
    avcodec_free_context(&f->ctx);
    av_frame_free(&f->frame);
    free(f->pcm);
    free(f);
    free(ad);
}

audio_decoder_t *audio_decoder_create_aac(void)
{
    ffmpeg_ad_t *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec) {
        free(f);
        return NULL;
    }
    f->ctx = avcodec_alloc_context3(codec);
    if (!f->ctx) {
        free(f);
        return NULL;
    }
    if (avcodec_open2(f->ctx, codec, NULL) < 0) {
        avcodec_free_context(&f->ctx);
        free(f);
        return NULL;
    }
    f->frame = av_frame_alloc();
    if (!f->frame) {
        avcodec_free_context(&f->ctx);
        free(f);
        return NULL;
    }

    audio_decoder_t *ad = calloc(1, sizeof(*ad));
    ad->priv = f;
    ad->submit = ad_submit;
    ad->pump = ad_pump;
    ad->destroy = ad_destroy;
    return ad;
}

#else /* !HAVE_FFMPEG */

audio_decoder_t *audio_decoder_create_aac(void)
{
    return NULL;
}

#endif /* HAVE_FFMPEG */
