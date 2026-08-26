#ifndef INPUT_PAD_H
#define INPUT_PAD_H

#include <stdint.h>

/* Moonlight (GameStream) gamepad button bit positions.
 * TODO: verify against moonlight-common-c BUTTON_* constants. */
#define BUTTON_UP     0x0001
#define BUTTON_DOWN   0x0002
#define BUTTON_LEFT   0x0004
#define BUTTON_RIGHT  0x0008
#define BUTTON_START  0x0010
#define BUTTON_SELECT 0x0020
#define BUTTON_L3     0x0040
#define BUTTON_R3     0x0080
#define BUTTON_L1     0x0100
#define BUTTON_R1     0x0200
#define BUTTON_L2     0x0400
#define BUTTON_R2     0x0800
#define BUTTON_A      0x1000  /* PS3 Cross  */
#define BUTTON_B      0x2000  /* PS3 Circle */
#define BUTTON_X      0x4000  /* PS3 Square */
#define BUTTON_Y      0x8000  /* PS3 Triangle */

/* A sampled gamepad state, already in Moonlight's value ranges. */
typedef struct {
    uint16_t buttons;       /* BUTTON_* mask */
    uint8_t  leftTrigger;   /* 0..255 */
    uint8_t  rightTrigger;  /* 0..255 */
    uint16_t leftX, leftY;  /* 0..65535, 32768 = center */
    uint16_t rightX, rightY;
} gamepad_state_t;

int  pad_init(void);
void pad_finalize(void);
/* Read pad on `port` (0-based) into st. Returns 0 on success, <0 if no data. */
int  pad_read(int port, gamepad_state_t *st);

#endif /* INPUT_PAD_H */
