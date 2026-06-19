#pragma once
#include <stddef.h>
#include <stdint.h>

// Sprite sheet = horizontal strip of equal-sized frames.
// Pixel format: 0xRRGGBBAA (RGBA8888, premultiplied not required; we use straight alpha).
typedef struct {
  uint32_t* pixels;  // owned
  int frame_w;
  int frame_h;
  int frame_count;
} sprite_sheet;

void sprite_sheet_free(sprite_sheet* s);

// Get a pointer to frame i's top-left pixel.
static inline const uint32_t* sprite_frame(const sprite_sheet* s, int i) {
  return s->pixels + (size_t)i * s->frame_w * s->frame_h;
}

typedef enum {
  ANIM_IDLE = 0,
  ANIM_BLINK,
  ANIM_SLEEP,
  ANIM_HAPPY,
  ANIM_SAD,
  ANIM_OBSERVE,
  ANIM_COUNT,
} anim_id;

// Procedurally build all default sprite sheets. Caller owns the array of ANIM_COUNT sheets.
sprite_sheet* procedural_build_all(void);
