#include "av/video_decoder.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FFMPEG

#include <libavcodec/avcodec.h>

typedef struct {
    AVCodecContext *ctx;
    AVFrame *frame;
    uint8_t *buf[3];
    size_t buflen[3];
} ffmpeg_vd_t;

static int vd_submit(video_decoder_t *vd, const uint8_t *data, size_t len, int64_t pts)
{
    ffmpeg_vd_t *f = vd->priv;
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = (uint8_t *)(uintptr_t)data;
    pkt.size = (int)len;
    pkt.pts = pts;
    int r = avcodec_send_packet(f->ctx, &pkt);
    av_packet_unref(&pkt);
    return (r == 0 || r == AVERROR(EAGAIN)) ? 0 : -1;
}

static int vd_pump(video_decoder_t *vd, video_frame_t *out)
{
    ffmpeg_vd_t *f = vd->priv;
    int r = avcodec_receive_frame(f->ctx, f->frame);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
        return 0;
    if (r < 0)
        return -1;

    AVFrame *fr = f->frame;
    int h = fr->height;
    size_t need[3] = {
        (size_t)fr->linesize[0] * h,
        (size_t)fr->linesize[1] * (h / 2),
        (size_t)fr->linesize[2] * (h / 2),
    };
    for (int i = 0; i < 3; i++) {
        if (f->buflen[i] < need[i]) {
            free(f->buf[i]);
            f->buf[i] = malloc(need[i]);
            f->buflen[i] = need[i];
        }
        memcpy(f->buf[i], fr->data[i], need[i]);
    }
    out->y = f->buf[0];
    out->u = f->buf[1];
    out->v = f->buf[2];
    out->width = fr->width;
    out->height = fr->height;
    for (int i = 0; i < 3; i++)
        out->linesize[i] = fr->linesize[i];
    out->pts = fr->pts;
    return 1;
}

static void vd_destroy(video_decoder_t *vd)
{
    ffmpeg_vd_t *f = vd->priv;
    avcodec_free_context(&f->ctx);
    av_frame_free(&f->frame);
    for (int i = 0; i < 3; i++)
        free(f->buf[i]);
    free(f);
    free(vd);
}

video_decoder_t *video_decoder_create_h264(void)
{
    ffmpeg_vd_t *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        free(f);
        return NULL;
    }
    f->ctx = avcodec_alloc_context3(codec);
    if (!f->ctx) {
        free(f);
        return NULL;
    }
    f->ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    f->ctx->flags2 |= AV_CODEC_FLAG2_FAST;
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

    video_decoder_t *vd = calloc(1, sizeof(*vd));
    vd->priv = f;
    vd->submit = vd_submit;
    vd->pump = vd_pump;
    vd->destroy = vd_destroy;
    return vd;
}

#else /* !HAVE_FFMPEG */

video_decoder_t *video_decoder_create_h264(void)
{
    return NULL;
}

#endif /* HAVE_FFMPEG */
