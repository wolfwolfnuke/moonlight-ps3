#ifndef NET_RTSP_H
#define NET_RTSP_H

#include "common/hoststore.h"

/* RTSP session state for a GameStream connection (host:48010). The actual
 * audio/video/control data arrives over separate UDP channels; this module
 * only drives the RTSP signaling (DESCRIBE/SETUP/PLAY/TEARDOWN). */
typedef struct {
    char host[HOST_IP_LEN];
    int  fd;                 /* RTSP TCP control socket */
    int  cseq;
    char session[64];        /* Session id returned by SETUP */
    int  video_local_port;   /* local UDP port we receive video on  (47998) */
    int  audio_local_port;   /* local UDP port we receive audio on  (47997) */
    int  control_local_port; /* local UDP port we receive control on (47999) */
    int  control_port;       /* server control port we SEND to        (47999) */
    int  width, height;      /* negotiated resolution from SDP (0=unknown) */
} rtsp_session_t;

#define MAX_APPS 64
typedef struct {
    char id[64];
    char name[128];
    int  running;   /* 1 if the app is currently running on the host */
} app_entry_t;

/* Open the RTSP TCP connection, DESCRIBE, and parse the SDP for the
 * stream ports. Returns 0 on success. */
int  rtsp_connect(rtsp_session_t *s, const paired_host_t *host);
/* SETUP the video/audio/control channels, capturing the Session id. */
int  rtsp_setup(rtsp_session_t *s);
/* PLAY -> streaming begins. */
int  rtsp_play(rtsp_session_t *s);
/* TEARDOWN and close the socket. */
void rtsp_teardown(rtsp_session_t *s);

/* App list + launch over the GameStream HTTPS API (port 47989). The exact
 * endpoint/port may need verification against moonlight-common-c (TODO). */
int  rtsp_applist(const paired_host_t *host, app_entry_t *out, int max);
int  rtsp_launch(const paired_host_t *host, const char *app_id);

#endif /* NET_RTSP_H */
