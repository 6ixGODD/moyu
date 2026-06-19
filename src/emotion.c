#include "emotion.h"

#include "sprite.h"

#include <math.h>

// xorshift64
static uint64_t xr_next(uint64_t* s) {
  uint64_t x = *s ? *s : 0x9e3779b97f4a7c15ULL;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *s = x;
  return x;
}

static float frand(uint64_t* s, float lo, float hi) {
  uint64_t r = xr_next(s);
  float f = (float)((r >> 11) & 0xffffff) / (float)0x1000000;
  return lo + f * (hi - lo);
}

void emotion_init(emotion* e, const personality* p) {
  e->valence = p->mood_bias;
  e->arousal = 0.0f;
  e->last_tick_ms = 0;
  e->rng_state = 0x12345678ULL;
}

void emotion_tick(emotion* e,
                  const personality* p,
                  uint64_t now_ms,
                  float dt_seconds) {
  if (e->last_tick_ms == 0) {
    e->last_tick_ms = now_ms;
    return;
  }
  // Drift toward personality's mood_bias; add small random walk.
  float pull_v = (p->mood_bias - e->valence) * 0.05f * dt_seconds;
  float pull_a = (0.0f - e->arousal) * 0.03f * dt_seconds;
  float noise_v = frand(&e->rng_state, -0.05f, 0.05f) * dt_seconds;
  float noise_a = frand(&e->rng_state, -0.05f, 0.05f) * dt_seconds;
  e->valence += pull_v + noise_v;
  e->arousal += pull_a + noise_a;
  if (e->valence < -1) e->valence = -1;
  if (e->valence > 1) e->valence = 1;
  if (e->arousal < -1) e->arousal = -1;
  if (e->arousal > 1) e->arousal = 1;
  e->last_tick_ms = now_ms;
}

int emotion_anim_hint(const emotion* e) {
  if (e->arousal < -0.5f) return ANIM_SLEEP;
  if (e->valence > 0.4f) return ANIM_HAPPY;
  if (e->valence < -0.4f) return ANIM_SAD;
  return ANIM_IDLE;
}
