#include "input/pad.h"
#include <io/pad.h>
#include "common/log.h"

#include <string.h>

int pad_init(void)
{
    /* PSL1GHT / cellPad is ready once the process is initialized; no explicit
     * init call is required for ioPadGetData. */
    return 0;
}

void pad_finalize(void)
{
}

int pad_read(int port, gamepad_state_t *st)
{
    padData data;
    if (ioPadGetData((u32)port, &data) < 0)
        return -1;

    memset(st, 0, sizeof(*st));

    uint16_t b = 0;
    if (data.BTN_UP)        b |= BUTTON_UP;
    if (data.BTN_DOWN)      b |= BUTTON_DOWN;
    if (data.BTN_LEFT)      b |= BUTTON_LEFT;
    if (data.BTN_RIGHT)     b |= BUTTON_RIGHT;
    if (data.BTN_START)     b |= BUTTON_START;
    if (data.BTN_SELECT)    b |= BUTTON_SELECT;
    if (data.BTN_L3)        b |= BUTTON_L3;
    if (data.BTN_R3)        b |= BUTTON_R3;
    if (data.BTN_L1)        b |= BUTTON_L1;
    if (data.BTN_R1)        b |= BUTTON_R1;
    if (data.BTN_L2)        b |= BUTTON_L2;
    if (data.BTN_R2)        b |= BUTTON_R2;
    if (data.BTN_CROSS)     b |= BUTTON_A;
    if (data.BTN_CIRCLE)    b |= BUTTON_B;
    if (data.BTN_SQUARE)    b |= BUTTON_X;
    if (data.BTN_TRIANGLE)  b |= BUTTON_Y;
    st->buttons = b;

    /* PS3 analog sticks report 0..255 (128 = center); Moonlight expects
     * 0..65535 (32768 = center). */
    st->leftX  = (uint16_t)((unsigned)data.ANA_L_H * 65535u / 255u);
    st->leftY  = (uint16_t)((unsigned)data.ANA_L_V * 65535u / 255u);
    st->rightX = (uint16_t)((unsigned)data.ANA_R_H * 65535u / 255u);
    st->rightY = (uint16_t)((unsigned)data.ANA_R_V * 65535u / 255u);
    st->leftTrigger  = 0; /* standard DS3 has no analog triggers */
    st->rightTrigger = 0;
    return 0;
}
