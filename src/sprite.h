#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Sprite sheet = horizontal strip of equal-sized frames.
// Pixel format: 0xRRGGBBAA (RGBA8888, straight alpha).
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
  ANIM_WALK,
  ANIM_WORK,
  ANIM_WAIT,
  ANIM_FOUND,
  ANIM_CONFUSED,
  ANIM_GIVEUP,
  ANIM_BORED,
  ANIM_SNEAK,
  ANIM_SCARED,
  ANIM_PLAYFUL,
  ANIM_PEEK,
  ANIM_DODGE,
  ANIM_STRETCH,
  ANIM_YAWN,
  ANIM_COUNT,
} anim_id;

#define ANIM_MAX_FRAMES 32

// A skin = one spritesheet + a per-animation list of frame indices + fps.
// This is the entire "appearance" of the pet, and it is fully Lua-configurable:
// the builtin procedural skin is just one possible skin; a BMP spritesheet
// loaded from disk is another. Animations are defined by which sheet frames
// they play and how fast — nothing else.
typedef struct {
  sprite_sheet sheet;
  int frames[ANIM_COUNT][ANIM_MAX_FRAMES];  // sheet frame indices, in play order
  int nframes[ANIM_COUNT];
  int fps[ANIM_COUNT];
} skin;

void skin_init_default(skin* sk);  // builtin 48px cream spirit + anim table
void skin_free(skin* sk);

// Load a BMP spritesheet and split it into fw x fh frames (row-major).
// On success, fills sk->sheet and leaves the anim table as-is (caller should
// call skin_set_anim afterwards). Returns false on any error.
bool skin_load_bmp(skin* sk, const char* path, int fw, int fh);

// Define an animation's frame list and speed. frames may have up to
// ANIM_MAX_FRAMES entries; n is clamped.
void skin_set_anim(skin* sk, int anim, const int* frames, int n, int fps);

// Name <-> id. Unknown name -> ANIM_IDLE.
int anim_id_from_name(const char* name);
const char* anim_name_from_id(int id);
