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

/* 3x5 font glyphs (rows are 3 bits, LSB = leftmost pixel). Index 0=' '(space)
 * .. then 'A'-'Z', '0'-'9', ':', '-'. Unknown chars render as blank. */
static const uint16_t font3x5[] = {
    /* space */ 0x000,
    /* A */ 0x7B7, /* B */ 0x6E7, /* C */ 0x327, /* D */ 0x6E9, /* E */ 0x767,
    /* F */ 0x757, /* G */ 0x3AF, /* H */ 0x757, /* I */ 0x492, /* J */ 0x31C,
    /* K */ 0x755, /* L */ 0x327, /* M */ 0x72F, /* N */ 0x72B, /* O */ 0x32B,
    /* P */ 0x6B7, /* Q */ 0x32B, /* R */ 0x6B5, /* S */ 0x74E, /* T */ 0x492,
    /* U */ 0x32D, /* V */ 0x1C7, /* W */ 0x3AD, /* X */ 0x5A5, /* Y */ 0x5A2,
    /* Z */ 0x4B3,
    /* 0 */ 0x72B, /* 1 */ 0x092, /* 2 */ 0x4B6, /* 3 */ 0x49E, /* 4 */ 0x53A,
    /* 5 */ 0x74E, /* 6 */ 0x76E, /* 7 */ 0x4A1, /* 8 */ 0x76F, /* 9 */ 0x76B,
    /* : */ 0x000, /* - */ 0x540,
};

static int font_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return 1 + (c - 'A');
    if (c >= '0' && c <= '9') return 1 + 26 + (c - '0');
    if (c == ':') return 1 + 26 + 10;
    if (c == '-') return 1 + 26 + 11;
    return 0;
}

void rsx_renderer_draw_text(rsx_renderer_t *r, int x, int y, int scale,
                            const char *s, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!r || !r->rgba || !s)
        return;

    int cx = x;
    for (const char *p = s; *p; p++) {
        uint16_t g = font3x5[font_index(*p)];
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if ((g >> (row * 3 + (2 - col))) & 1) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = cx + col * scale + sx;
                            int py = y + row * scale + sy;
                            if (px < 0 || py < 0 || px >= r->width || py >= r->height)
                                continue;
                            uint8_t *d = r->rgba + ((size_t)py * r->width + px) * 4;
                            d[0] = red; d[1] = green; d[2] = blue; d[3] = 255;
                        }
                    }
                }
            }
        }
        cx += 4 * scale; /* 3 cols + 1 space */
    }
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
