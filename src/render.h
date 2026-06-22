#pragma once
#include "platform.h"
#include "sprite.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t* buf;  // owned, w*h RGBA8888 (0xRRGGBBAA)
  int w, h;
} render_ctx;

render_ctx render_new(int w, int h);
void render_free(render_ctx* r);

void render_clear(render_ctx* r, uint32_t color);
// Blit a sprite frame at (dx,dy). Transparent source pixels (alpha==0) skipped.
void render_blit_frame(
    render_ctx* r, const sprite_sheet* s, int frame, int dx, int dy);
// Integer-scale blit (nearest-neighbor). scale=1 equivalent to render_blit_frame.
void render_blit_frame_scaled(
    render_ctx* r, const sprite_sheet* s, int frame, int dx, int dy, int scale);
void render_blit_frame_scaled_flipped(
    render_ctx* r, const sprite_sheet* s, int frame, int dx, int dy, int scale);
// Blit with a global alpha modulation (multiplies source alpha).
void render_blit_frame_alpha(render_ctx* r,
                             const sprite_sheet* s,
                             int frame,
                             int dx,
                             int dy,
                             uint8_t alpha_mul);

// Push the backbuffer to the platform window.
void render_present(render_ctx* r, platform_window* w);
