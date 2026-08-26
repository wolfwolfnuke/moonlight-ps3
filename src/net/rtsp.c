#include "net/rtsp.h"
#include "sock.h"
#include "common/log.h"
#include "net/http.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#define RTSP_PORT 48010

static int rtsp_send(rtsp_session_t *s, const char *req)
{
    return net_send(s->fd, req, (int)strlen(req));
}

/* Read RTSP response headers (up to the blank line) into buf. Returns bytes
 * read, or -1 on error. Does not consume a body (callers parse the SDP that
 * follows via a second read if Content-Length is present). */
static int rtsp_read_headers(rtsp_session_t *s, char *buf, int cap)
{
    int used = 0;
    char c;
    int prev1 = 0, prev2 = 0;
    while (used < cap - 1) {
        int n = net_recv_timeout(s->fd, &c, 1, 2000);
        if (n <= 0)
            break;
        buf[used++] = c;
        if (prev1 == '\r' && prev2 == '\n' && c == '\r') {
            /* potential end of headers; read the trailing \n */
            int n2 = net_recv_timeout(s->fd, &c, 1, 2000);
            if (n2 > 0) buf[used++] = c;
            break;
        }
        prev2 = prev1; prev1 = c;
    }
    buf[used] = 0;
    return used;
}

static int rtsp_read_body(rtsp_session_t *s, char *buf, int cap, int len)
{
    int got = 0;
    while (got < len && got < cap - 1) {
        int n = net_recv_timeout(s->fd, buf + got, len - got, 2000);
        if (n <= 0)
            break;
        got += n;
    }
    buf[got] = 0;
    return got;
}

static const char *rtsp_header(const char *hdr, const char *name)
{
    static char val[256];
    const char *p = strstr(hdr, name);
    if (!p)
        return NULL;
    p += strlen(name);
    while (*p == ' ' || *p == '\t') p++;
    const char *e = strstr(p, "\r\n");
    int n = e ? (int)(e - p) : (int)strlen(p);
    if (n >= (int)sizeof(val)) n = (int)sizeof(val) - 1;
    memcpy(val, p, n); val[n] = 0;
    return val;
}

/* Parse "m=video <port> ..." / "m=audio <port> ..." from the SDP body. */
static int sdp_port(const char *sdp, const char *media)
{
    char tag[32];
    snprintf(tag, sizeof(tag), "m=%s ", media);
    const char *p = strstr(sdp, tag);
    if (!p)
        return -1;
    return atoi(p + strlen(tag));
}

/* Parse "a=framesize:96 W-H" from the SDP body. Returns 0 on success. */
static int sdp_framesize(const char *sdp, int *w, int *h)
{
    const char *p = strstr(sdp, "a=framesize:");
    if (!p)
        return -1;
    p = strchr(p, ' ');
    if (!p)
        return -1;
    int W = atoi(p + 1);
    const char *dash = strchr(p + 1, '-');
    if (!dash)
        return -1;
    int H = atoi(dash + 1);
    if (W > 0 && H > 0) {
        *w = W; *h = H;
        return 0;
    }
    return -1;
}

int rtsp_connect(rtsp_session_t *s, const paired_host_t *host)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->host, host->ip, HOST_IP_LEN - 1);
    s->video_local_port   = 47998;
    s->audio_local_port   = 47997;
    s->control_local_port = 47999;
    s->control_port       = 47999;
    s->cseq = 1;

    s->fd = net_connect(s->host, RTSP_PORT);
    if (s->fd < 0)
        return -1;

    char req[512];
    snprintf(req, sizeof(req),
        "DESCRIBE rtsp://%s:%d RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "User-Agent: Moonlight-PS3\r\n"
        "Accept: application/sdp\r\n\r\n",
        s->host, RTSP_PORT, s->cseq++);

    if (rtsp_send(s, req) < 0) {
        LOGE("rtsp: DESCRIBE send failed\n");
        net_close(s->fd); s->fd = -1;
        return -1;
    }

    char hdr[2048];
    if (rtsp_read_headers(s, hdr, sizeof(hdr)) <= 0) {
        LOGE("rtsp: no DESCRIBE response\n");
        net_close(s->fd); s->fd = -1;
        return -1;
    }
    if (strstr(hdr, "RTSP/1.0 200") == NULL) {
        LOGE("rtsp: DESCRIBE rejected: %s\n", hdr);
        net_close(s->fd); s->fd = -1;
        return -1;
    }

    const char *cl = rtsp_header(hdr, "Content-Length:");
    if (cl) {
        int len = atoi(cl);
        char sdp[4096];
        if (rtsp_read_body(s, sdp, sizeof(sdp), len) > 0) {
            int v = sdp_port(sdp, "video");
            int a = sdp_port(sdp, "audio");
            int w = 0, h = 0;
            if (sdp_framesize(sdp, &w, &h) == 0)
                LOGI("rtsp: sdp video_port=%d audio_port=%d resolution=%dx%d\n", v, a, w, h);
            else
                LOGI("rtsp: sdp video_port=%d audio_port=%d (no framesize)\n", v, a);
            s->width = w;
            s->height = h;
        }
    }
    return 0;
}

int rtsp_setup(rtsp_session_t *s)
{
    /* SETUP video (streamid=0), audio (streamid=1), control (streamid=2).
     * TODO: verify the exact Transport string Sunshine expects (client_port
     * ranges / unicast flag). We send a plausible form and capture Session. */
    const char *streams[3] = { "0", "1", "2" };
    int local[3] = { s->video_local_port, s->audio_local_port, s->control_local_port };
    for (int i = 0; i < 3; i++) {
        char req[512];
        snprintf(req, sizeof(req),
            "SETUP rtsp://%s:%d/streamid=%s RTSP/1.0\r\n"
            "CSeq: %d\r\n"
            "Transport: unicast;client_port=%d\r\n"
            "User-Agent: Moonlight-PS3\r\n\r\n",
            s->host, RTSP_PORT, streams[i], s->cseq++, local[i]);
        if (rtsp_send(s, req) < 0) {
            LOGE("rtsp: SETUP stream %s send failed\n", streams[i]);
            return -1;
        }
        char hdr[2048];
        if (rtsp_read_headers(s, hdr, sizeof(hdr)) <= 0 ||
            strstr(hdr, "RTSP/1.0 200") == NULL) {
            LOGE("rtsp: SETUP stream %s failed\n", streams[i]);
            return -1;
        }
        const char *sess = rtsp_header(hdr, "Session:");
        if (sess && s->session[0] == 0)
            strncpy(s->session, sess, sizeof(s->session) - 1);
    }
    if (s->session[0] == 0) {
        LOGE("rtsp: no Session id from SETUP\n");
        return -1;
    }
    return 0;
}

int rtsp_play(rtsp_session_t *s)
{
    char req[512];
    snprintf(req, sizeof(req),
        "PLAY rtsp://%s:%d RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Session: %s\r\n"
        "Range: npt=0.000-\r\n"
        "User-Agent: Moonlight-PS3\r\n\r\n",
        s->host, RTSP_PORT, s->cseq++, s->session);
    if (rtsp_send(s, req) < 0) {
        LOGE("rtsp: PLAY send failed\n");
        return -1;
    }
    char hdr[2048];
    if (rtsp_read_headers(s, hdr, sizeof(hdr)) <= 0 ||
        strstr(hdr, "RTSP/1.0 200") == NULL) {
        LOGE("rtsp: PLAY rejected\n");
        return -1;
    }
    LOGI("rtsp: PLAY ok, streaming started\n");
    return 0;
}

void rtsp_teardown(rtsp_session_t *s)
{
    if (s->fd < 0)
        return;
    char req[512];
    snprintf(req, sizeof(req),
        "TEARDOWN rtsp://%s:%d RTSP/1.0\r\n"
        "CSeq: %d\r\n"
        "Session: %s\r\n"
        "User-Agent: Moonlight-PS3\r\n\r\n",
        s->host, RTSP_PORT, s->cseq++, s->session);
    rtsp_send(s, req);
    net_close(s->fd);
    s->fd = -1;
}

/* GameStream serves app list + launch over HTTPS. The port below (47989) is the
 * standard Moonlight HTTPS port; verify against moonlight-common-c (TODO). */
#define APPLIST_PORT 47989

int rtsp_applist(const paired_host_t *host, app_entry_t *out, int max)
{
    char url[128];
    snprintf(url, sizeof(url), "https://%s:%d/applist", host->ip, APPLIST_PORT);

    http_response_t r;
    if (http_get(url, host, &r) != 0 || r.status != 200 || !r.body) {
        LOGE("rtsp: applist request failed (status %d)\n", r.status);
        http_response_free(&r);
        return -1;
    }

    int n = 0;
    xmlDocPtr doc = xmlParseMemory(r.body, (int)r.body_len);
    if (doc) {
        xmlNodePtr root = xmlDocGetRootElement(doc);
        for (xmlNodePtr cur = root ? root->children : NULL; cur && n < max; cur = cur->next) {
            if (cur->type != XML_ELEMENT_NODE ||
                xmlStrcmp(cur->name, (const xmlChar *)"app") != 0)
                continue;
            app_entry_t *a = &out[n];
            memset(a, 0, sizeof(*a));
            for (xmlNodePtr c = cur->children; c; c = c->next) {
                if (c->type != XML_ELEMENT_NODE)
                    continue;
                xmlChar *v = xmlNodeListGetString(doc, c->children, 1);
                if (!v)
                    continue;
                if (xmlStrcmp(c->name, (const xmlChar *)"id") == 0)
                    snprintf(a->id, sizeof(a->id), "%s", (char *)v);
                else if (xmlStrcmp(c->name, (const xmlChar *)"AppTitle") == 0)
                    snprintf(a->name, sizeof(a->name), "%s", (char *)v);
                else if (xmlStrcmp(c->name, (const xmlChar *)"IsRunning") == 0)
                    a->running = (strcmp((char *)v, "1") == 0) ? 1 : 0;
                xmlFree(v);
            }
            if (a->id[0])
                n++;
        }
        xmlFreeDoc(doc);
    } else {
        LOGE("rtsp: applist XML parse failed\n");
    }
    http_response_free(&r);
    LOGI("rtsp: got %d app(s)\n", n);
    return n;
}

int rtsp_launch(const paired_host_t *host, const char *app_id)
{
    char url[160];
    snprintf(url, sizeof(url), "https://%s:%d/launch?appid=%s",
             host->ip, APPLIST_PORT, app_id);

    http_response_t r;
    int rc = http_post(url, host, "", &r);
    int status = r.status;
    http_response_free(&r);
    LOGI("rtsp: launch appid=%s -> rc=%d status=%d\n", app_id, rc, status);
    return (rc == 0 && (status == 200 || status == 302)) ? 0 : -1;
}
