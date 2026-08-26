#ifndef PROTO_INPUT_PACKETS_H
#define PROTO_INPUT_PACKETS_H

#include <stdint.h>
#include "net/rtsp.h"
#include "net/udp_stream.h"
#include "common/hoststore.h"

/* Context describing the control channel for sending input. */
typedef struct {
    int      control_fd;   /* client UDP socket (also used to send) */
    const char *host;      /* server ip */
    int      control_port; /* server control port */
    uint8_t  key[16];      /* pairing session key */
} input_ctx_t;

void input_ctx_init(input_ctx_t *ic, const udp_stream_t *u,
                    const rtsp_session_t *s, const paired_host_t *h);

/* Build an AES-encrypted control packet and send it to the host.
 * Values follow the Moonlight (GameStream) big-endian wire layout.
 * TODO: verify exact packet byte layout / type constants. */
int input_send_gamepad(const input_ctx_t *ic, uint16_t buttons,
                       uint8_t lt, uint8_t rt,
                       uint16_t lx, uint16_t ly,
                       uint16_t rx, uint16_t ry);
int input_send_keyboard(const input_ctx_t *ic, uint16_t keycode,
                        uint8_t modifier, uint8_t flags);
int input_send_mouse(const input_ctx_t *ic, int16_t dx, int16_t dy,
                     uint8_t buttons);

#endif /* PROTO_INPUT_PACKETS_H */
