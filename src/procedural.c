#include "sprite.h"

#include "image.h"
#include "log.h"
#include "mem.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Builtin procedural skin: a cute round blob mascot with big eyes, blush, and
// little ears. 20 frames of 32x32, laid out as:
//   idle    0..3    blink   4..5    sleep   6..9
//   happy  10..13   sad    14..17   observe 18..19
// Everything here is plain code — no asset files — so the default pet is
// guaranteed visible with zero dependencies.
// ---------------------------------------------------------------------------

#define FW 32
#define FH 32

// NOTE: these are deliberately NOT const. An earlier version declared them
// `static const uint32_t C_X = 0;` and mutated them via cast in init_colors();
// under /O2 the compiler folded them to 0, so every body/eye/mouth pixel was
// fully transparent and the pet was invisible. Keep them mutable.
static uint32_t C_BODY, C_BODY_DARK, C_OUTLINE, C_WHITE, C_DARK, C_BLUSH, C_Z,
    C_TEAR, C_EAR;

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return rgba(r, g, b, 255);
}

static void init_colors(void) {
  C_BODY = rgb(122, 201, 236);      // sky blue
  C_BODY_DARK = rgb(92, 164, 206);  // underside shadow
  C_OUTLINE = rgb(44, 60, 92);      // dark outline
  C_WHITE = rgb(250, 250, 255);
  C_DARK = rgb(40, 50, 70);
  C_BLUSH = rgb(245, 150, 165);
  C_Z = rgb(170, 195, 220);
  C_TEAR = rgb(120, 180, 240);
  C_EAR = rgb(150, 215, 240);
}

static void put_px(uint32_t* f, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= FW || y >= FH) return;
  f[y * FW + x] = c;
}

static void fill_ellipse(
    uint32_t* f, int cx, int cy, int rx, int ry, uint32_t c) {
  for (int y = -ry; y <= ry; y++) {
    for (int x = -rx; x <= rx; x++) {
      // (x/rx)^2 + (y/ry)^2 <= 1, scaled to avoid floats
      long dx = (long)x * 16 / (rx > 0 ? rx : 1);
      long dy = (long)y * 16 / (ry > 0 ? ry : 1);
      if (dx * dx + dy * dy <= 256) put_px(f, cx + x, cy + y, c);
    }
  }
}

static void rect_filled(
    uint32_t* f, int x0, int y0, int x1, int y1, uint32_t c) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      put_px(f, x, y, c);
}

static void hline(uint32_t* f, int x0, int x1, int y, uint32_t c) {
  for (int x = x0; x <= x1; x++)
    put_px(f, x, y, c);
}

static void clear_frame(uint32_t* f) {
  for (int i = 0; i < FW * FH; i++)
    f[i] = 0;
}

// Draw the body: outline ellipse, body fill, underside shadow, two ears.
static void draw_body(uint32_t* f, int yoff, int squish) {
  int rx = 11 + squish;
  int ry = 9 - squish;
  int cx = 16, cy = 19 + yoff;
  // outline
  fill_ellipse(f, cx, cy, rx + 1, ry + 1, C_OUTLINE);
  // body
  fill_ellipse(f, cx, cy, rx, ry, C_BODY);
  // underside shadow
  fill_ellipse(f, cx, cy + 3, rx - 2, ry - 4, C_BODY_DARK);
  // ears: two little round bumps on top
  fill_ellipse(f, 9, 9 + yoff, 3, 3, C_OUTLINE);
  fill_ellipse(f, 9, 9 + yoff, 2, 2, C_EAR);
  fill_ellipse(f, 23, 9 + yoff, 3, 3, C_OUTLINE);
  fill_ellipse(f, 23, 9 + yoff, 2, 2, C_EAR);
}

// eye_mode: 0 open, 1 closed/blink, 2 happy ^_^
// dirx: -1 look left, 0 center, 1 look right
static void draw_eyes(uint32_t* f, int yoff, int eye_mode, int dirx) {
  int lx = 9, rx = 19, ey = 14 + yoff;
  if (eye_mode == 0) {
    // white sclera 4x5, dark pupil 2x2 shifted by dirx
    rect_filled(f, lx, ey, lx + 3, ey + 4, C_WHITE);
    rect_filled(f, rx, ey, rx + 3, ey + 4, C_WHITE);
    rect_filled(f, lx + 1 + dirx, ey + 1, lx + 2 + dirx, ey + 2, C_DARK);
    rect_filled(f, rx + 1 + dirx, ey + 1, rx + 2 + dirx, ey + 2, C_DARK);
    // tiny glint
    put_px(f, lx + 1 + dirx, ey + 1, C_WHITE);
    put_px(f, rx + 1 + dirx, ey + 1, C_WHITE);
  } else if (eye_mode == 1) {
    // closed: gentle curved line
    hline(f, lx, lx + 3, ey + 2, C_DARK);
    hline(f, rx, rx + 3, ey + 2, C_DARK);
  } else {  // happy ^_^
    put_px(f, lx, ey + 3, C_DARK);
    put_px(f, lx + 1, ey + 2, C_DARK);
    put_px(f, lx + 2, ey + 2, C_DARK);
    put_px(f, lx + 3, ey + 3, C_DARK);
    put_px(f, rx, ey + 3, C_DARK);
    put_px(f, rx + 1, ey + 2, C_DARK);
    put_px(f, rx + 2, ey + 2, C_DARK);
    put_px(f, rx + 3, ey + 3, C_DARK);
  }
}

// mouth_mode: 0 neutral, 1 smile, 2 frown, 3 small-o
static void draw_mouth(uint32_t* f, int yoff, int mouth_mode) {
  int mx = 16, my = 22 + yoff;
  switch (mouth_mode) {
    case 0: hline(f, mx - 1, mx + 1, my, C_DARK); break;
    case 1:  // smile
      put_px(f, mx - 2, my, C_DARK);
      put_px(f, mx - 1, my + 1, C_DARK);
      put_px(f, mx, my + 1, C_DARK);
      put_px(f, mx + 1, my + 1, C_DARK);
      put_px(f, mx + 2, my, C_DARK);
      break;
    case 2:  // frown
      put_px(f, mx - 2, my + 1, C_DARK);
      put_px(f, mx - 1, my, C_DARK);
      put_px(f, mx, my, C_DARK);
      put_px(f, mx + 1, my, C_DARK);
      put_px(f, mx + 2, my + 1, C_DARK);
      break;
    case 3:  // small-o
      put_px(f, mx - 1, my, C_DARK);
      put_px(f, mx + 1, my, C_DARK);
      put_px(f, mx, my + 1, C_DARK);
      break;
  }
}

static void draw_blush(uint32_t* f, int yoff) {
  put_px(f, 7, 20 + yoff, C_BLUSH);
  put_px(f, 8, 20 + yoff, C_BLUSH);
  put_px(f, 23, 20 + yoff, C_BLUSH);
  put_px(f, 24, 20 + yoff, C_BLUSH);
}

static void draw_z(uint32_t* f, int frame) {
  int x = 22, y = 4 + (frame % 2);
  hline(f, x, x + 3, y, C_Z);
  put_px(f, x + 2, y + 1, C_Z);
  put_px(f, x + 1, y + 2, C_Z);
  hline(f, x, x + 3, y + 3, C_Z);
}

static void draw_tear(uint32_t* f, int yoff) {
  put_px(f, 23, 19 + yoff, C_TEAR);
  put_px(f, 23, 20 + yoff, C_TEAR);
}

static sprite_sheet make_sheet(int frames) {
  sprite_sheet s;
  s.frame_w = FW;
  s.frame_h = FH;
  s.frame_count = frames;
  s.pixels = (uint32_t*)moyu_alloc((size_t)FW * FH * frames * 4);
  memset(s.pixels, 0, (size_t)FW * FH * frames * 4);
  return s;
}

static uint32_t* frame_at(sprite_sheet* s, int i) {
  return s->pixels + (size_t)i * FW * FH;
}

void skin_init_default(skin* sk) {
  init_colors();
  memset(sk, 0, sizeof(*sk));
  // 20 frames
  sk->sheet = make_sheet(20);

  // idle 0..3: gentle bob
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, i);
    clear_frame(f);
    int yoff = (i % 2 == 0) ? 0 : -1;
    draw_body(f, yoff, 0);
    draw_eyes(f, yoff, 0, 0);
    draw_mouth(f, yoff, 0);
  }
  // blink 4..5
  for (int i = 0; i < 2; i++) {
    uint32_t* f = frame_at(&sk->sheet, 4 + i);
    clear_frame(f);
    draw_body(f, 0, 0);
    draw_eyes(f, 0, i == 0 ? 0 : 1, 0);
    draw_mouth(f, 0, 0);
  }
  // sleep 6..9
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, 6 + i);
    clear_frame(f);
    int yoff = (i % 2 == 0) ? 1 : 0;
    draw_body(f, yoff, 1);
    draw_eyes(f, yoff, 1, 0);
    draw_mouth(f, yoff, 0);
    draw_z(f, i);
  }
  // happy 10..13: bounce + smile + blush
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, 10 + i);
    clear_frame(f);
    int yoff = (i < 2) ? -2 : 0;
    draw_body(f, yoff, 0);
    draw_eyes(f, yoff, 2, 0);
    draw_mouth(f, yoff, 1);
    draw_blush(f, yoff);
  }
  // sad 14..17: droop + frown + tear
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, 14 + i);
    clear_frame(f);
    int yoff = 1;
    draw_body(f, yoff, 1);
    draw_eyes(f, yoff, 0, 0);
    draw_mouth(f, yoff, 2);
    if (i % 2 == 0) draw_tear(f, yoff);
  }
  // observe 18..19: eyes look left then right
  for (int i = 0; i < 2; i++) {
    uint32_t* f = frame_at(&sk->sheet, 18 + i);
    clear_frame(f);
    draw_body(f, 0, 0);
    draw_eyes(f, 0, 0, i == 0 ? -1 : 1);
    draw_mouth(f, 0, 3);
  }

  // default anim table
  static const int def[ANIM_COUNT][4] = {
      {0, 1, 2, 3},          // idle
      {4, 5, 4, 5},          // blink
      {6, 7, 8, 9},          // sleep
      {10, 11, 12, 13},      // happy
      {14, 15, 16, 17},      // sad
      {18, 19, 18, 19},      // observe
  };
  static const int def_fps[ANIM_COUNT] = {3, 5, 3, 8, 3, 4};
  static const int def_n[ANIM_COUNT] = {4, 4, 4, 4, 4, 4};
  for (int a = 0; a < ANIM_COUNT; a++) {
    sk->nframes[a] = def_n[a];
    sk->fps[a] = def_fps[a];
    for (int j = 0; j < def_n[a]; j++)
      sk->frames[a][j] = def[a][j];
  }
}

void skin_free(skin* sk) {
  if (!sk) return;
  sprite_sheet_free(&sk->sheet);
}

bool skin_load_bmp(skin* sk, const char* path, int fw, int fh) {
  uint32_t* px = NULL;
  int w = 0, h = 0;
  if (!image_load_bmp(path, &px, &w, &h)) {
    LOGW("skin: failed to load BMP %s", path);
    return false;
  }
  if (fw <= 0 || fh <= 0 || fw > w || fh > h || w % fw != 0 || h % fh != 0) {
    LOGW("skin: BMP %s is %dx%d, not divisible by frame %dx%d", path, w, h, fw, fh);
    moyu_free(px);
    return false;
  }
  sprite_sheet_free(&sk->sheet);
  sk->sheet.pixels = px;
  sk->sheet.frame_w = fw;
  sk->sheet.frame_h = fh;
  sk->sheet.frame_count = (w / fw) * (h / fh);
  LOGI("skin: loaded %s (%dx%d -> %d frames of %dx%d)",
       path, w, h, sk->sheet.frame_count, fw, fh);
  return true;
}

void skin_set_anim(skin* sk, int anim, const int* frames, int n, int fps) {
  if (anim < 0 || anim >= ANIM_COUNT || !frames) return;
  if (n < 0) n = 0;
  if (n > ANIM_MAX_FRAMES) n = ANIM_MAX_FRAMES;
  for (int i = 0; i < n; i++) {
    int fi = frames[i];
    // clamp into range so a bad index never reads out of bounds
    if (sk->sheet.frame_count > 0)
      fi %= sk->sheet.frame_count;
    sk->frames[anim][i] = fi;
  }
  sk->nframes[anim] = n;
  sk->fps[anim] = fps > 0 ? fps : 4;
}

static const char* ANIM_NAMES[ANIM_COUNT] = {
    "idle", "blink", "sleep", "happy", "sad", "observe"};

int anim_id_from_name(const char* name) {
  if (!name) return ANIM_IDLE;
  for (int i = 0; i < ANIM_COUNT; i++)
    if (strcmp(name, ANIM_NAMES[i]) == 0) return i;
  return ANIM_IDLE;
}

const char* anim_name_from_id(int id) {
  if (id < 0 || id >= ANIM_COUNT) return "idle";
  return ANIM_NAMES[id];
}
