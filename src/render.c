#include "render.h"

#include "mem.h"
#include "platform.h"

#include <string.h>

render_ctx render_new(int w, int h) {
  render_ctx r;
  r.w = w;
  r.h = h;
  r.buf = (uint32_t*)moyu_alloc((size_t)w * h * 4);
  memset(r.buf, 0, (size_t)w * h * 4);
  return r;
}

void render_free(render_ctx* r) {
  if (!r) return;
  if (r->buf) moyu_free(r->buf);
  r->buf = NULL;
}

void render_clear(render_ctx* r, uint32_t color) {
  for (int i = 0; i < r->w * r->h; i++)
    r->buf[i] = color;
}

void render_blit_frame(
    render_ctx* r, const sprite_sheet* s, int frame, int dx, int dy) {
  if (!s || frame < 0 || frame >= s->frame_count) return;
  const uint32_t* src = sprite_frame(s, frame);
  for (int y = 0; y < s->frame_h; y++) {
    int ty = dy + y;
    if (ty < 0 || ty >= r->h) continue;
    for (int x = 0; x < s->frame_w; x++) {
      int tx = dx + x;
      if (tx < 0 || tx >= r->w) continue;
      uint32_t p = src[y * s->frame_w + x];
      uint8_t a = (uint8_t)(p & 0xff);
      if (a == 0) continue;
      if (a == 255) {
        r->buf[ty * r->w + tx] = p;
      } else {
        // straight-alpha blend
        uint32_t dst = r->buf[ty * r->w + tx];
        uint8_t dr = (dst >> 24) & 0xff, dg = (dst >> 16) & 0xff,
                db = (dst >> 8) & 0xff, da = dst & 0xff;
        uint8_t sr = (p >> 24) & 0xff, sg = (p >> 16) & 0xff,
                sb = (p >> 8) & 0xff;
        int inv = 255 - a;
        uint8_t orr = (uint8_t)((sr * a + dr * inv) / 255);
        uint8_t og = (uint8_t)((sg * a + dg * inv) / 255);
        uint8_t ob = (uint8_t)((sb * a + db * inv) / 255);
        uint8_t oa = (uint8_t)(a + (da * inv) / 255);
        r->buf[ty * r->w + tx] = ((uint32_t)orr << 24) | ((uint32_t)og << 16) |
                                 ((uint32_t)ob << 8) | oa;
      }
    }
  }
}

void render_blit_frame_alpha(render_ctx* r,
                             const sprite_sheet* s,
                             int frame,
                             int dx,
                             int dy,
                             uint8_t alpha_mul) {
  if (!s || frame < 0 || frame >= s->frame_count) return;
  const uint32_t* src = sprite_frame(s, frame);
  for (int y = 0; y < s->frame_h; y++) {
    int ty = dy + y;
    if (ty < 0 || ty >= r->h) continue;
    for (int x = 0; x < s->frame_w; x++) {
      int tx = dx + x;
      if (tx < 0 || tx >= r->w) continue;
      uint32_t p = src[y * s->frame_w + x];
      uint8_t a = (uint8_t)((p & 0xff) * alpha_mul / 255);
      if (a == 0) continue;
      uint8_t sr = (p >> 24) & 0xff, sg = (p >> 16) & 0xff,
              sb = (p >> 8) & 0xff;
      uint32_t pp =
          ((uint32_t)sr << 24) | ((uint32_t)sg << 16) | ((uint32_t)sb << 8) | a;
      // write a temporary single-pixel buffer and reuse blit logic
      uint32_t dst = r->buf[ty * r->w + tx];
      uint8_t dr = (dst >> 24) & 0xff, dg = (dst >> 16) & 0xff,
              db = (dst >> 8) & 0xff, da = dst & 0xff;
      int inv = 255 - a;
      uint8_t orr = (uint8_t)((sr * a + dr * inv) / 255);
      uint8_t og = (uint8_t)((sg * a + dg * inv) / 255);
      uint8_t ob = (uint8_t)((sb * a + db * inv) / 255);
      uint8_t oa = (uint8_t)(a + (da * inv) / 255);
      r->buf[ty * r->w + tx] = ((uint32_t)orr << 24) | ((uint32_t)og << 16) |
                               ((uint32_t)ob << 8) | oa;
      (void)pp;
    }
  }
}

void render_present(render_ctx* r, platform_window* w) {
  platform_window_set_pixels(w, r->buf, r->w, r->h);
}

void render_blit_frame_scaled(render_ctx* r,
                              const sprite_sheet* s,
                              int frame,
                              int dx,
                              int dy,
                              int scale) {
  if (!s || frame < 0 || frame >= s->frame_count || scale < 1) return;
  if (scale == 1) {
    render_blit_frame(r, s, frame, dx, dy);
    return;
  }
  const uint32_t* src = sprite_frame(s, frame);
  for (int y = 0; y < s->frame_h; y++) {
    for (int x = 0; x < s->frame_w; x++) {
      uint32_t p = src[y * s->frame_w + x];
      uint8_t a = (uint8_t)(p & 0xff);
      if (a == 0) continue;
      for (int sy = 0; sy < scale; sy++) {
        int ty = dy + y * scale + sy;
        if (ty < 0 || ty >= r->h) continue;
        for (int sx = 0; sx < scale; sx++) {
          int tx = dx + x * scale + sx;
          if (tx < 0 || tx >= r->w) continue;
          r->buf[ty * r->w + tx] = p;
        }
      }
    }
  }
}
