#include "proto/input_packets.h"
#include "net/udp_stream.h"
#include "net/sock.h"
#include "common/log.h"

#include <arpa/inet.h>
#include <string.h>

#define INPUT_TYPE_GAMEPAD 0x0A
#define INPUT_TYPE_KEYBOARD 0x0C
#define INPUT_TYPE_MOUSE    0x0B

/* Moonlight control input packet header (big-endian on the wire). */
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t size;
    uint16_t unused;
} nv_input_header_t;

void input_ctx_init(input_ctx_t *ic, const udp_stream_t *u,
                    const rtsp_session_t *s, const paired_host_t *h)
{
    ic->control_fd   = u->control_fd;
    ic->host         = s->host;
    ic->control_port = s->control_port;
    memcpy(ic->key, h->key, 16);
}

static int send_control(const input_ctx_t *ic, const void *pkt, size_t pktlen)
{
    uint8_t buf[256];
    int n = control_encrypt(buf, sizeof(buf), (const uint8_t *)pkt, pktlen,
                            ic->key);
    if (n < 0) {
        LOGE("input: control_encrypt failed\n");
        return -1;
    }
    if (net_sendto(ic->control_fd, buf, n, ic->host, ic->control_port) < 0) {
        LOGE("input: send failed\n");
        return -1;
    }
    return 0;
}

int input_send_gamepad(const input_ctx_t *ic, uint16_t buttons,
                       uint8_t lt, uint8_t rt,
                       uint16_t lx, uint16_t ly,
                       uint16_t rx, uint16_t ry)
{
    typedef struct __attribute__((packed)) {
        nv_input_header_t h;
        uint16_t buttonFlags;
        uint8_t  leftTrigger;
        uint8_t  rightTrigger;
        uint16_t leftStickX;
        uint16_t leftStickY;
        uint16_t rightStickX;
        uint16_t rightStickY;
    } gp_t;

    gp_t p;
    memset(&p, 0, sizeof(p));
    p.h.type = htons(INPUT_TYPE_GAMEPAD);
    p.h.size = htons(sizeof(p));
    p.h.unused = 0;
    p.buttonFlags = htons(buttons);
    p.leftTrigger  = lt;
    p.rightTrigger = rt;
    p.leftStickX   = htons(lx);
    p.leftStickY   = htons(ly);
    p.rightStickX  = htons(rx);
    p.rightStickY  = htons(ry);
    return send_control(ic, &p, sizeof(p));
}

int input_send_keyboard(const input_ctx_t *ic, uint16_t keycode,
                        uint8_t modifier, uint8_t flags)
{
    typedef struct __attribute__((packed)) {
        nv_input_header_t h;
        uint16_t keyCode;
        uint8_t  modifier;
        uint8_t  flags;
    } kb_t;

    kb_t p;
    memset(&p, 0, sizeof(p));
    p.h.type = htons(INPUT_TYPE_KEYBOARD);
    p.h.size = htons(sizeof(p));
    p.h.unused = 0;
    p.keyCode  = htons(keycode);
    p.modifier = modifier;
    p.flags    = flags;
    return send_control(ic, &p, sizeof(p));
}

int input_send_mouse(const input_ctx_t *ic, int16_t dx, int16_t dy,
                     uint8_t buttons)
{
    typedef struct __attribute__((packed)) {
        nv_input_header_t h;
        uint16_t deltaX;
        uint16_t deltaY;
        uint8_t  buttons;
    } ms_t;

    ms_t p;
    memset(&p, 0, sizeof(p));
    p.h.type = htons(INPUT_TYPE_MOUSE);
    p.h.size = htons(sizeof(p));
    p.h.unused = 0;
    p.deltaX  = htons((uint16_t)dx);
    p.deltaY  = htons((uint16_t)dy);
    p.buttons = buttons;
    return send_control(ic, &p, sizeof(p));
}
