#include "render/rsx_renderer.h"

#include <stdlib.h>
#include <string.h>

#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

struct rsx_renderer {
    int width, height;
    gcmContextData *ctx;
    uint8_t *rgba;     /* CPU-side staging buffer (TODO: RSX memory + textures) */
    size_t rgba_len;
};

rsx_renderer_t *rsx_renderer_init(int width, int height)
{
    rsx_renderer_t *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->width = width;
    r->height = height;

    /* Best-effort RSX bring-up. The full framebuffer/tile/shader setup required
     * for a real flip is a TODO; this only obtains a context handle. */
    if (rsxInit(&r->ctx, 0x10000, 0x100000, NULL) != 0) {
        /* Non-fatal for the scaffold: rendering falls back to the CPU blit. */
        r->ctx = NULL;
    }

    r->rgba_len = (size_t)width * height * 4;
    r->rgba = malloc(r->rgba_len);
    if (!r->rgba) {
        free(r);
        return NULL;
    }
    return r;
}

static inline uint8_t clamp_u8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : (uint8_t)v);
}

void rsx_renderer_present_yuv(rsx_renderer_t *r, const video_frame_t *f)
{
    if (!r || !r->rgba || !f)
        return;

    int w = f->width, h = f->height;
    const uint8_t *y = f->y, *u = f->u, *v = f->v;
    size_t out = (size_t)w * h * 4;
    if (out > r->rgba_len)
        return;

    for (int yy = 0; yy < h; yy++) {
        int yrow = yy * f->linesize[0];
        int uvrow = (yy >> 1) * f->linesize[1];
        for (int xx = 0; xx < w; xx++) {
            int Y = y[yrow + xx] - 16;
            int U = u[uvrow + (xx >> 1)] - 128;
            int V = v[uvrow + (xx >> 1)] - 128;

            int r0 = (298 * Y + 409 * V + 128) >> 8;
            int g0 = (298 * Y - 100 * U - 208 * V + 128) >> 8;
            int b0 = (298 * Y + 516 * U + 128) >> 8;

            uint8_t *p = r->rgba + ((size_t)yy * w + xx) * 4;
            p[0] = clamp_u8(r0);
            p[1] = clamp_u8(g0);
            p[2] = clamp_u8(b0);
            p[3] = 255;
        }
    }

    /* TODO: upload r->rgba as a texture (or, better, upload the three Y/U/V
     * planes and convert in yuv2rgb.frag) and issue the RSX draw + flip. */
}

void rsx_renderer_shutdown(rsx_renderer_t *r)
{
    if (!r)
        return;
    free(r->rgba);
    /* TODO: release RSX resources (no exported rsxExit in PSL1GHT). */
    r->ctx = NULL;
    free(r);
}

void rsx_renderer_clear(rsx_renderer_t *r, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!r || !r->rgba)
        return;
    uint32_t *p = (uint32_t *)r->rgba;
    uint32_t px = (uint32_t)red | ((uint32_t)green << 8) | ((uint32_t)blue << 16) | 0xff000000u;
    for (size_t i = 0; i < r->rgba_len / 4; i++)
        p[i] = px;
    /* TODO: upload to RSX and flip (see present_yuv). */
}
