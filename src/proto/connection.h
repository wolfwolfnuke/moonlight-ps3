#ifndef PROTO_CONNECTION_H
#define PROTO_CONNECTION_H

#include "common/hoststore.h"
#include "net/rtsp.h"
#include "net/udp_stream.h"
#include "net/reassembly.h"
#include "av/pipeline.h"

/* Full streaming session: RTSP signaling plus the three UDP channels. */
typedef struct {
    rtsp_session_t rtsp;
    udp_stream_t   udp;
    paired_host_t *host;
    pipeline_t    *pipeline;   /* media decode/render/audio (NULL w/o FFmpeg) */
    reasm_t       *vreasm;     /* video datagram reassembly */
    reasm_t       *areasm;     /* audio datagram reassembly */
} session_t;

/* Bring up the session: RTSP DESCRIBE/SETUP/PLAY then open UDP receivers. */
int  session_start(paired_host_t *host, session_t *out);
void session_stop(session_t *s);

/* Receive one batch of video/audio datagrams from the UDP channels, feed them
 * to the pipeline, and present/play. Call repeatedly from the stream loop.
 * No-op when the pipeline is unavailable. */
int  session_pump(session_t *s);

#endif /* PROTO_CONNECTION_H */
