#include "sprite.h"

#include "image.h"
#include "log.h"
#include "mem.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Builtin procedural skin: a MINIMALIST blob. One rounded body, two dot eyes,
// a tiny mouth. No ears, no glint, no highlight, no blush — just enough to
// read as a small living creature. The whole point is "simple and clean".
//
// 20 frames of 32x32, same layout the anim table expects:
//   idle 0..3   blink 4..5   sleep 6..9   happy 10..13   sad 14..17   observe 18..19
// ---------------------------------------------------------------------------

#define FW 32
#define FH 32

// Mutable (NOT const — see git history: const-folded colors made the pet
// invisible under /O2).
static uint32_t C_BODY, C_OUTLINE, C_DARK, C_Z, C_TEAR;

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 255u;
}

static void init_colors(void) {
  C_BODY = rgb(244, 241, 234);     // warm off-white (reads clean on any desktop)
  C_OUTLINE = rgb(54, 54, 64);     // soft charcoal outline
  C_DARK = rgb(48, 48, 58);        // eyes / mouth
  C_Z = rgb(150, 150, 165);
  C_TEAR = rgb(140, 185, 235);
}

static void put_px(uint32_t* f, int x, int y, uint32_t c) {
  if (x < 0 || y < 0 || x >= FW || y >= FH) return;
  f[y * FW + x] = c;
}

static void fill_ellipse(
    uint32_t* f, int cx, int cy, int rx, int ry, uint32_t c) {
  for (int y = -ry; y <= ry; y++) {
    for (int x = -rx; x <= rx; x++) {
      long dx = (long)x * 16 / (rx > 0 ? rx : 1);
      long dy = (long)y * 16 / (ry > 0 ? ry : 1);
      if (dx * dx + dy * dy <= 256) put_px(f, cx + x, cy + y, c);
    }
  }
}

static void hline(uint32_t* f, int x0, int x1, int y, uint32_t c) {
  for (int x = x0; x <= x1; x++) put_px(f, x, y, c);
}

static void clear_frame(uint32_t* f) {
  for (int i = 0; i < FW * FH; i++) f[i] = 0;
}

// Minimal body: outline ring + single fill. Nothing else.
static void draw_body(uint32_t* f, int yoff, int squish) {
  int rx = 9 + squish;
  int ry = 8 - squish;
  int cx = 16, cy = 18 + yoff;
  fill_ellipse(f, cx, cy, rx + 1, ry + 1, C_OUTLINE);
  fill_ellipse(f, cx, cy, rx, ry, C_BODY);
}

// eye_mode: 0 open dots, 1 closed line, 2 happy ^^
// dirx: -1 / 0 / +1 look direction (observe)
static void draw_eyes(uint32_t* f, int yoff, int eye_mode, int dirx) {
  int ly = 16 + yoff;
  if (eye_mode == 0) {
    put_px(f, 11 + dirx, ly, C_DARK);
    put_px(f, 12 + dirx, ly, C_DARK);
    put_px(f, 11 + dirx, ly + 1, C_DARK);
    put_px(f, 12 + dirx, ly + 1, C_DARK);
    put_px(f, 19 + dirx, ly, C_DARK);
    put_px(f, 20 + dirx, ly, C_DARK);
    put_px(f, 19 + dirx, ly + 1, C_DARK);
    put_px(f, 20 + dirx, ly + 1, C_DARK);
  } else if (eye_mode == 1) {
    hline(f, 11, 12, ly + 1, C_DARK);
    hline(f, 19, 20, ly + 1, C_DARK);
  } else {
    put_px(f, 11, ly + 1, C_DARK);
    put_px(f, 12, ly, C_DARK);
    put_px(f, 19, ly + 1, C_DARK);
    put_px(f, 20, ly, C_DARK);
  }
}

// mouth_mode: 0 none, 1 smile, 2 frown, 3 small-o
static void draw_mouth(uint32_t* f, int yoff, int mouth_mode) {
  int mx = 16, my = 21 + yoff;
  switch (mouth_mode) {
    case 0: put_px(f, mx, my, C_DARK); break;  // tiny neutral dot
    case 1:
      put_px(f, mx - 1, my, C_DARK);
      put_px(f, mx, my + 1, C_DARK);
      put_px(f, mx + 1, my, C_DARK);
      break;  // smile
    case 2:
      put_px(f, mx - 1, my + 1, C_DARK);
      put_px(f, mx, my, C_DARK);
      put_px(f, mx + 1, my + 1, C_DARK);
      break;  // frown
    case 3:
      put_px(f, mx, my, C_DARK);
      put_px(f, mx - 1, my + 1, C_DARK);
      put_px(f, mx + 1, my + 1, C_DARK);
      break;  // o
  }
}

static void draw_z(uint32_t* f, int frame) {
  int x = 22, y = 4 + (frame % 2);
  hline(f, x, x + 2, y, C_Z);
  put_px(f, x + 1, y + 1, C_Z);
  hline(f, x, x + 2, y + 2, C_Z);
}

static void draw_tear(uint32_t* f, int yoff) {
  put_px(f, 21, 18 + yoff, C_TEAR);
  put_px(f, 21, 19 + yoff, C_TEAR);
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
  sk->sheet = make_sheet(20);

  // idle 0..3: gentle bob, neutral
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
  // sleep 6..9: closed eyes + Z
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, 6 + i);
    clear_frame(f);
    int yoff = (i % 2 == 0) ? 1 : 0;
    draw_body(f, yoff, 1);
    draw_eyes(f, yoff, 1, 0);
    draw_mouth(f, yoff, 0);
    draw_z(f, i);
  }
  // happy 10..13: bounce + smile
  for (int i = 0; i < 4; i++) {
    uint32_t* f = frame_at(&sk->sheet, 10 + i);
    clear_frame(f);
    int yoff = (i < 2) ? -2 : 0;
    draw_body(f, yoff, 0);
    draw_eyes(f, yoff, 2, 0);
    draw_mouth(f, yoff, 1);
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
  // observe 18..19: eyes look left/right
  for (int i = 0; i < 2; i++) {
    uint32_t* f = frame_at(&sk->sheet, 18 + i);
    clear_frame(f);
    draw_body(f, 0, 0);
    draw_eyes(f, 0, 0, i == 0 ? -1 : 1);
    draw_mouth(f, 0, 3);
  }

  static const int def[ANIM_COUNT][4] = {
      {0, 1, 2, 3}, {4, 5, 4, 5}, {6, 7, 8, 9},
      {10, 11, 12, 13}, {14, 15, 16, 17}, {18, 19, 18, 19},
  };
  static const int def_fps[ANIM_COUNT] = {3, 5, 3, 8, 3, 4};
  static const int def_n[ANIM_COUNT] = {4, 4, 4, 4, 4, 4};
  for (int a = 0; a < ANIM_COUNT; a++) {
    sk->nframes[a] = def_n[a];
    sk->fps[a] = def_fps[a];
    for (int j = 0; j < def_n[a]; j++) sk->frames[a][j] = def[a][j];
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
    if (sk->sheet.frame_count > 0) fi %= sk->sheet.frame_count;
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
