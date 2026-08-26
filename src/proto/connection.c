#include "proto/connection.h"
#include "common/log.h"
#include "net/reassembly.h"

#include <string.h>

static void on_video_frame(void *ctx, const uint8_t *f, size_t len, int64_t pts)
{
    session_t *s = ctx;
    pipeline_submit_video(s->pipeline, f, len, pts);
}

static void on_audio_frame(void *ctx, const uint8_t *f, size_t len, int64_t pts)
{
    session_t *s = ctx;
    pipeline_submit_audio(s->pipeline, f, len, pts);
}

int session_start(paired_host_t *host, session_t *out)
{
    memset(out, 0, sizeof(*out));
    out->host = host;

    if (rtsp_connect(&out->rtsp, host) != 0) {
        LOGE("session: rtsp_connect failed\n");
        return -1;
    }
    if (rtsp_setup(&out->rtsp) != 0) {
        LOGE("session: rtsp_setup failed\n");
        rtsp_teardown(&out->rtsp);
        return -1;
    }
    if (rtsp_play(&out->rtsp) != 0) {
        LOGE("session: rtsp_play failed\n");
        rtsp_teardown(&out->rtsp);
        return -1;
    }
    if (udp_open(&out->udp, &out->rtsp) != 0) {
        LOGE("session: udp_open failed\n");
        rtsp_teardown(&out->rtsp);
        return -1;
    }

    /* Negotiated resolution from the SDP (fallback to 480p). */
    int w = out->rtsp.width > 0 ? out->rtsp.width : 854;
    int h = out->rtsp.height > 0 ? out->rtsp.height : 480;
    out->pipeline = pipeline_create(w, h);

    out->vreasm = reasm_create(1);
    reasm_set_callback(out->vreasm, on_video_frame, out);
    out->areasm = reasm_create(0);
    reasm_set_callback(out->areasm, on_audio_frame, out);

    LOGI("session: started with %s\n", host->ip);
    return 0;
}

int session_pump(session_t *s)
{
    if (!s->pipeline)
        return 0;

    static uint8_t buf[65536];
    int n;

    n = udp_recv(&s->udp, UDP_VIDEO, buf, sizeof(buf), 0);
    if (n > 0)
        reasm_feed(s->vreasm, buf, (size_t)n);

    n = udp_recv(&s->udp, UDP_AUDIO, buf, sizeof(buf), 0);
    if (n > 0)
        reasm_feed(s->areasm, buf, (size_t)n);

    pipeline_pump(s->pipeline);
    return 0;
}

void session_stop(session_t *s)
{
    if (s->pipeline) {
        pipeline_destroy(s->pipeline);
        s->pipeline = NULL;
    }
    reasm_destroy(s->vreasm);
    reasm_destroy(s->areasm);
    s->vreasm = s->areasm = NULL;
    udp_close(&s->udp);
    rtsp_teardown(&s->rtsp);
    s->host = NULL;
}
