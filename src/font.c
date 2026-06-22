#include "font.h"

#include "log.h"
#include "mem.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

// 5x7 ASCII bitmap font (printable 32..126).
static const uint8_t FONT[96][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00},
    {0x00, 0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00, 0x00},
    {0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00},
    {0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00},
    {0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00},
    {0x08, 0x2A, 0x1C, 0x2A, 0x08, 0x00, 0x00},
    {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00},
    {0x00, 0x50, 0x30, 0x00, 0x00, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00},
    {0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00},
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00},
    {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00},
    {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00, 0x00},
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00},
    {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00},
    {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, 0x00},
    {0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00},
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00},
    {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00},
    {0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00},
    {0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00},
    {0x02, 0x01, 0x51, 0x09, 0x06, 0x00, 0x00},
    {0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, 0x00},
    {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, 0x00},
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00},
    {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00},
    {0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00},
    {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00},
    {0x7F, 0x09, 0x09, 0x01, 0x01, 0x00, 0x00},
    {0x3E, 0x41, 0x41, 0x51, 0x32, 0x00, 0x00},
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00},
    {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00},
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00},
    {0x7F, 0x02, 0x04, 0x02, 0x7F, 0x00, 0x00},
    {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00},
    {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00},
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00},
    {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, 0x00},
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00},
    {0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00},
    {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00},
    {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00},
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00},
    {0x7F, 0x20, 0x18, 0x20, 0x7F, 0x00, 0x00},
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00},
    {0x03, 0x04, 0x78, 0x04, 0x03, 0x00, 0x00},
    {0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00},
    {0x00, 0x00, 0x7F, 0x41, 0x41, 0x00, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x00},
    {0x41, 0x41, 0x7F, 0x00, 0x00, 0x00, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04, 0x00, 0x00},
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00},
    {0x00, 0x01, 0x02, 0x04, 0x00, 0x00, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78, 0x00, 0x00},
    {0x7F, 0x48, 0x44, 0x44, 0x38, 0x00, 0x00},
    {0x38, 0x44, 0x44, 0x44, 0x20, 0x00, 0x00},
    {0x38, 0x44, 0x44, 0x48, 0x7F, 0x00, 0x00},
    {0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x00},
    {0x08, 0x7E, 0x09, 0x01, 0x02, 0x00, 0x00},
    {0x08, 0x14, 0x54, 0x54, 0x3C, 0x00, 0x00},
    {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00},
    {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00, 0x00, 0x00},
    {0x00, 0x7F, 0x10, 0x28, 0x44, 0x00, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78, 0x00, 0x00},
    {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00, 0x00},
    {0x38, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00},
    {0x7C, 0x14, 0x14, 0x14, 0x08, 0x00, 0x00},
    {0x08, 0x14, 0x14, 0x18, 0x7C, 0x00, 0x00},
    {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00, 0x00},
    {0x48, 0x54, 0x54, 0x54, 0x20, 0x00, 0x00},
    {0x04, 0x3F, 0x44, 0x40, 0x20, 0x00, 0x00},
    {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00, 0x00},
    {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00, 0x00},
    {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00, 0x00},
    {0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00},
    {0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00, 0x00},
    {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x00},
    {0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00, 0x00, 0x00},
    {0x02, 0x01, 0x02, 0x04, 0x02, 0x00, 0x00},
};

#define ASCII_W 5
#define ASCII_H 7
#define ASCII_ADV (ASCII_W + 1)  // 6

#define CJK_PX 12
#define CJK_ADV (CJK_PX + 1)  // 13

// Decode one UTF-8 codepoint starting at *i; advance *i. Returns codepoint.
static uint32_t utf8_next(const char* s, size_t len, size_t* i) {
  if (*i >= len) return 0;
  unsigned char c = (unsigned char)s[*i];
  if (c < 0x80) {
    (*i)++;
    return c;
  }
  if ((c & 0xE0) == 0xC0 && *i + 1 < len) {
    uint32_t cp = ((c & 0x1F) << 6) | ((unsigned char)s[*i + 1] & 0x3F);
    *i += 2;
    return cp;
  }
  if ((c & 0xF0) == 0xE0 && *i + 2 < len) {
    uint32_t cp = ((c & 0x0F) << 12) |
                  (((unsigned char)s[*i + 1] & 0x3F) << 6) |
                  ((unsigned char)s[*i + 2] & 0x3F);
    *i += 3;
    return cp;
  }
  if ((c & 0xF8) == 0xF0 && *i + 3 < len) {
    uint32_t cp = ((c & 0x07) << 18) |
                  (((unsigned char)s[*i + 1] & 0x3F) << 12) |
                  (((unsigned char)s[*i + 2] & 0x3F) << 6) |
                  ((unsigned char)s[*i + 3] & 0x3F);
    *i += 4;
    return cp;
  }
  (*i)++;
  return '?';
}

static int char_advance(uint32_t cp) {
  if (cp < 0x80) return ASCII_ADV;
  return CJK_ADV;
}

static void put_pixel(render_ctx* r, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
  r->buf[y * r->w + x] = c;
}

static void draw_ascii(render_ctx* r, char ch, int dx, int dy, uint32_t fg) {
  if (ch < 32 || ch > 126) ch = '?';
  const uint8_t* g = FONT[ch - 32];
  for (int row = 0; row < ASCII_H; row++) {
    uint8_t bits = g[row];
    for (int col = 0; col < ASCII_W; col++) {
      if (bits & (0x10 >> col)) put_pixel(r, dx + col, dy + row, fg);
    }
  }
}

static void draw_cjk(render_ctx* r, uint32_t cp, int dx, int dy, uint32_t fg) {
  int gw = 0, gh = 0;
  uint8_t* bits = platform_get_glyph(cp, CJK_PX, &gw, &gh);
  if (!bits || gw <= 0 || gh <= 0) {
    // fallback: draw a box
    for (int y = 0; y < CJK_PX; y++)
      for (int x = 0; x < CJK_PX; x++)
        if (x == 0 || y == 0 || x == CJK_PX - 1 || y == CJK_PX - 1)
          put_pixel(r, dx + x, dy + y, fg);
    if (bits) moyu_free(bits);
    return;
  }
  int row_bytes = ((gw + 31) / 32) * 4;
  for (int y = 0; y < gh; y++) {
    for (int x = 0; x < gw; x++) {
      uint8_t byte = bits[y * row_bytes + (x / 8)];
      if (byte & (0x80 >> (x % 8))) {
        put_pixel(r, dx + x, dy + y, fg);
      }
    }
  }
  moyu_free(bits);
}

static void draw_codepoint(
    render_ctx* r, uint32_t cp, int dx, int dy, uint32_t fg) {
  if (cp < 0x80)
    draw_ascii(r, (char)cp, dx, dy, fg);
  else
    draw_cjk(r, cp, dx, dy, fg);
}

static bubble_rect render_bubble_legacy(render_ctx* r,
                                        const char* text,
                                        int dx,
                                        int dy_top) {
  bubble_rect out = {0, 0, 0, 0};
  if (!text) return out;
  size_t len = strlen(text);
  if (len == 0) return out;

  // Compute text width & wrap into lines.
  // Max bubble width = min(r->w - 8, 200).
  int max_w = r->w - 8;
  if (max_w > 200) max_w = 200;
  if (max_w < 40) max_w = 40;

  // Tokenize into codepoints, then greedily wrap.
  typedef struct {
    int x;
    uint32_t cp;
  } tok;
  tok* toks = (tok*)moyu_alloc(len * sizeof(tok));
  int ntok = 0;
  {
    size_t i = 0;
    int x = 0;
    while (i < len) {
      uint32_t cp = utf8_next(text, len, &i);
      toks[ntok].cp = cp;
      toks[ntok].x = x;
      x += char_advance(cp);
      ntok++;
    }
  }
  int total_w =
      ntok > 0 ? (toks[ntok - 1].x + char_advance(toks[ntok - 1].cp)) : 0;

  // Greedy wrap: break when next token would exceed max_w; also break on '\n'.
  int* line_start = (int*)moyu_alloc((ntok + 1) * sizeof(int));
  int* line_end = (int*)moyu_alloc((ntok + 1) * sizeof(int));
  int nlines = 0;
  int i = 0;
  while (i < ntok) {
    int start = i;
    int x = 0;
    while (i < ntok) {
      if (toks[i].cp == '\n') {
        i++;
        break;
      }
      int adv = char_advance(toks[i].cp);
      if (x + adv > max_w && i > start) break;
      x += adv;
      i++;
    }
    line_start[nlines] = start;
    line_end[nlines] = i;
    nlines++;
  }

  int pad = 4;
  int line_h = CJK_PX + 2;  // CJK is the taller of the two
  int text_h = nlines * line_h;
  int bw = (total_w < max_w ? total_w : max_w) + pad * 2;
  int bh = text_h + pad * 2;

  int bx = dx - bw / 2;
  int by = dy_top - bh;         // bubble above dy_top
  if (by < 0) by = dy_top + 4;  // flip below

  out.x = bx;
  out.y = by;
  out.w = bw;
  out.h = bh;

  // Bubble background (opaque white) + dark border.
  uint32_t bg = 0xFFFFFFFFu;
  uint32_t border = 0x303038FFu;
  for (int y = 0; y < bh; y++) {
    for (int x = 0; x < bw; x++) {
      int tx = bx + x, ty = by + y;
      if (tx < 0 || ty < 0 || tx >= r->w || ty >= r->h) continue;
      uint32_t c = bg;
      if (x == 0 || y == 0 || x == bw - 1 || y == bh - 1) c = border;
      r->buf[ty * r->w + tx] = c;
    }
  }

  uint32_t fg = 0x202028FFu;
  for (int li = 0; li < nlines; li++) {
    int tx = bx + pad;
    int ty = by + pad + 2;  // small top inset
    // Center line horizontally if narrower than bubble
    int line_w = 0;
    for (int k = line_start[li]; k < line_end[li]; k++)
      line_w += char_advance(toks[k].cp);
    tx += (bw - pad * 2 - line_w) / 2;
    int base_y = ty + li * line_h;
    for (int k = line_start[li]; k < line_end[li]; k++) {
      uint32_t cp = toks[k].cp;
      // Vertical: ASCII glyphs are 7px tall, CJK 12px. Align to common top+2.
      int yoff = (cp < 0x80) ? 2 : 0;
      draw_codepoint(r, cp, tx, base_y + yoff, fg);
      tx += char_advance(cp);
    }
  }

  moyu_free(toks);
  moyu_free(line_start);
  moyu_free(line_end);
  return out;
}

static void blend_pixel(render_ctx* r, int x, int y, uint32_t src) {
  if (x < 0 || y < 0 || x >= r->w || y >= r->h) return;
  uint8_t a = (uint8_t)(src & 0xff);
  if (!a) return;
  if (a == 255) {
    r->buf[y * r->w + x] = src;
    return;
  }
  uint32_t dst = r->buf[y * r->w + x];
  int inv = 255 - a;
  uint8_t sr = (uint8_t)(src >> 24), sg = (uint8_t)(src >> 16);
  uint8_t sb = (uint8_t)(src >> 8);
  uint8_t dr = (uint8_t)(dst >> 24), dg = (uint8_t)(dst >> 16);
  uint8_t db = (uint8_t)(dst >> 8), da = (uint8_t)dst;
  uint8_t rr = (uint8_t)((sr * a + dr * inv) / 255);
  uint8_t gg = (uint8_t)((sg * a + dg * inv) / 255);
  uint8_t bb = (uint8_t)((sb * a + db * inv) / 255);
  uint8_t aa = (uint8_t)(a + da * inv / 255);
  r->buf[y * r->w + x] = ((uint32_t)rr << 24) | ((uint32_t)gg << 16) |
                          ((uint32_t)bb << 8) | aa;
}

static bool inside_pixel_round_rect(int x, int y, int w, int h, int cut) {
  if (x < 0 || y < 0 || x >= w || y >= h) return false;
  if (x >= cut && x < w - cut) return true;
  if (y >= cut && y < h - cut) return true;
  int cx = x < cut ? cut : w - cut - 1;
  int cy = y < cut ? cut : h - cut - 1;
  int ax = x - cx, ay = y - cy;
  return ax * ax + ay * ay <= cut * cut;
}

bubble_rect render_bubble(render_ctx* r, const char* text, int dx, int dy_top) {
  bubble_rect out = {0, 0, 0, 0};
  if (!r || !text || !text[0]) return out;

  int max_text_w = r->w - 38;
  if (max_text_w > 210) max_text_w = 210;
  if (max_text_w < 48) return render_bubble_legacy(r, text, dx, dy_top);

  platform_text_bitmap glyphs = {0};
  if (!platform_render_text(text, 14, max_text_w, 0x302A26FFu, &glyphs))
    return render_bubble_legacy(r, text, dx, dy_top);

  const int pad_x = 10, pad_y = 7, tail_h = 7, shadow = 2;
  int bw = glyphs.w + pad_x * 2;
  int body_h = glyphs.h + pad_y * 2;
  int bh = body_h + tail_h + shadow;
  int bx = dx - bw / 2;
  if (bx < 3) bx = 3;
  if (bx + bw + shadow > r->w - 3) bx = r->w - 3 - bw - shadow;
  int by = dy_top - bh;
  bool below = false;
  if (by < 2) {
    below = true;
    by = dy_top + 5;
  }

  const uint32_t shadow_c = 0x4B41382Eu;
  const uint32_t border = 0x665B50FFu;
  const uint32_t paper = 0xFFF9EEFFu;
  const int radius = 7;
  int body_y = by + (below ? tail_h : 0);
  for (int y = 0; y < body_h; y++) {
    for (int x = 0; x < bw; x++) {
      if (inside_pixel_round_rect(x, y, bw, body_h, radius))
        blend_pixel(r, bx + x + shadow, body_y + y + shadow, shadow_c);
    }
  }
  for (int y = 0; y < body_h; y++) {
    for (int x = 0; x < bw; x++) {
      if (!inside_pixel_round_rect(x, y, bw, body_h, radius)) continue;
      bool edge = !inside_pixel_round_rect(x - 1, y, bw, body_h, radius) ||
                  !inside_pixel_round_rect(x + 1, y, bw, body_h, radius) ||
                  !inside_pixel_round_rect(x, y - 1, bw, body_h, radius) ||
                  !inside_pixel_round_rect(x, y + 1, bw, body_h, radius);
      blend_pixel(r, bx + x, body_y + y, edge ? border : paper);
    }
  }

  int tail_x = dx;
  if (tail_x < bx + 12) tail_x = bx + 12;
  if (tail_x > bx + bw - 13) tail_x = bx + bw - 13;
  for (int row = 0; row < tail_h; row++) {
    int half = 4 - row / 2;
    int yy = below ? body_y - 1 - row : body_y + body_h + row;
    for (int x = -half; x <= half; x++) {
      bool edge = x == -half || x == half || row == tail_h - 1;
      blend_pixel(r, tail_x + x, yy, edge ? border : paper);
    }
  }

  int tx = bx + pad_x;
  int ty = body_y + pad_y;
  for (int y = 0; y < glyphs.h; y++)
    for (int x = 0; x < glyphs.w; x++)
      blend_pixel(r, tx + x, ty + y, glyphs.pixels[y * glyphs.w + x]);

  out.x = bx;
  out.y = by;
  out.w = bw + shadow;
  out.h = bh;
  platform_text_bitmap_free(&glyphs);
  return out;
}

bubble_rect render_info_card(render_ctx* r,
                             const char* title,
                             const char* body,
                             int dx,
                             int dy_top,
                             int max_w) {
  bubble_rect out = {0, 0, 0, 0};
  if (!title || !title[0]) return out;
  if (max_w <= 0) max_w = 176;
  if (max_w > r->w - 12) max_w = r->w - 12;
  if (max_w < 72) max_w = 72;

  char merged[1024];
  if (body && body[0])
    snprintf(merged, sizeof(merged), "%s\n%s", title, body);
  else
    snprintf(merged, sizeof(merged), "%s", title);

  size_t len = strlen(merged);
  typedef struct {
    int x;
    uint32_t cp;
  } tok;
  tok* toks = (tok*)moyu_alloc((len + 1) * sizeof(tok));
  int ntok = 0;
  {
    size_t i = 0;
    int x = 0;
    while (i < len) {
      uint32_t cp = utf8_next(merged, len, &i);
      toks[ntok].cp = cp;
      toks[ntok].x = x;
      x += char_advance(cp);
      ntok++;
    }
  }
  int* line_start = (int*)moyu_alloc((ntok + 2) * sizeof(int));
  int* line_end = (int*)moyu_alloc((ntok + 2) * sizeof(int));
  int* line_width = (int*)moyu_alloc((ntok + 2) * sizeof(int));
  int nlines = 0;
  int i = 0;
  while (i < ntok) {
    int start = i;
    int x = 0;
    while (i < ntok) {
      if (toks[i].cp == '\n') {
        i++;
        break;
      }
      int adv = char_advance(toks[i].cp);
      if (x + adv > max_w - 18 && i > start) break;
      x += adv;
      i++;
    }
    line_start[nlines] = start;
    line_end[nlines] = i;
    line_width[nlines] = x;
    nlines++;
  }
  int line_h = CJK_PX + 2;
  int pad = 6;
  int card_w = 0;
  for (i = 0; i < nlines; i++)
    if (line_width[i] > card_w) card_w = line_width[i];
  card_w += pad * 2 + 10;
  if (card_w < 92) card_w = 92;
  int card_h = pad * 2 + nlines * line_h + 4;
  int bx = dx - card_w / 2;
  int by = dy_top - card_h;
  if (bx < 4) bx = 4;
  if (bx + card_w > r->w - 4) bx = r->w - 4 - card_w;
  if (by < 0) by = 0;
  out.x = bx;
  out.y = by;
  out.w = card_w;
  out.h = card_h;

  uint32_t bg = 0xF9F3EAFFu;
  uint32_t border = 0x6A5D50FFu;
  uint32_t accent = 0xA8B89FFFu;
  uint32_t fg = 0x2C2622FFu;
  for (int y = 0; y < card_h; y++) {
    for (int x = 0; x < card_w; x++) {
      int tx = bx + x;
      int ty = by + y;
      if (tx < 0 || ty < 0 || tx >= r->w || ty >= r->h) continue;
      int rx = x < 5 ? 5 - x : (x >= card_w - 5 ? x - (card_w - 6) : 0);
      int ry = y < 5 ? 5 - y : (y >= card_h - 5 ? y - (card_h - 6) : 0);
      if (rx && ry && rx * rx + ry * ry > 25) continue;
      uint32_t c = bg;
      if (x == 0 || y == 0 || x == card_w - 1 || y == card_h - 1) c = border;
      if (x >= 4 && x <= 8 && y >= 5 && y < card_h - 5) c = accent;
      r->buf[ty * r->w + tx] = c;
    }
  }

  for (int li = 0; li < nlines; li++) {
    int tx = bx + pad + 12;
    int base_y = by + pad + li * line_h;
    for (int k = line_start[li]; k < line_end[li]; k++) {
      uint32_t cp = toks[k].cp;
      int yoff = (cp < 0x80) ? 2 : 0;
      draw_codepoint(r, cp, tx, base_y + yoff, fg);
      tx += char_advance(cp);
    }
  }

  moyu_free(toks);
  moyu_free(line_start);
  moyu_free(line_end);
  moyu_free(line_width);
  return out;
}
