#ifndef RENDER_RSX_RENDERER_H
#define RENDER_RSX_RENDERER_H

#include "av/video_decoder.h"

typedef struct rsx_renderer rsx_renderer_t;

/* Initialise the RSX for a given output resolution. Returns NULL on failure. */
rsx_renderer_t *rsx_renderer_init(int width, int height);

/* Present a decoded YUV420 frame. Currently performs a CPU YUV->RGBA blit into
 * an RSX-resident buffer; the GPU texturing + yuv2rgb.frag shader path is a
 * TODO (see rsx_renderer.c). */
void rsx_renderer_present_yuv(rsx_renderer_t *r, const video_frame_t *f);

/* Fill the framebuffer with a solid color (placeholder for real UI rendering). */
void rsx_renderer_clear(rsx_renderer_t *r, uint8_t red, uint8_t green, uint8_t blue);

/* Draw a NUL-terminated string using a built-in 3x5 font (uppercase A-Z,
 * 0-9, space, ':' and '-' supported). Writes into the CPU staging buffer that
 * is later presented. scale is the pixel size of one font cell. */
void rsx_renderer_draw_text(rsx_renderer_t *r, int x, int y, int scale,
                            const char *s, uint8_t red, uint8_t green, uint8_t blue);

void rsx_renderer_shutdown(rsx_renderer_t *r);

#endif /* RENDER_RSX_RENDERER_H */
