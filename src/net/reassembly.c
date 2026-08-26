#include "net/reassembly.h"

#include <stdlib.h>

struct reasm {
    reasm_frame_cb cb;
    void          *ctx;
};

reasm_t *reasm_create(void)
{
    return calloc(1, sizeof(reasm_t));
}

void reasm_destroy(reasm_t *r)
{
    free(r);
}

void reasm_set_callback(reasm_t *r, reasm_frame_cb cb, void *ctx)
{
    r->cb  = cb;
    r->ctx = ctx;
}

void reasm_feed(reasm_t *r, const uint8_t *data, size_t len)
{
    if (!r || !r->cb)
        return;

    /* TODO: real GameStream defragmentation. Until the wire header is decoded
     * from moonlight-common-c, emit each datagram as a complete frame. */
    r->cb(r->ctx, data, len, 0);
}
