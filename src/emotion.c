#include "emotion.h"

#include "sprite.h"

#include <math.h>

static float clampf(float v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }

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
  // Decay toward personality baselines. State only changes abruptly when a
  // real event calls emotion_react().
  float pull_v = (p->mood_bias - e->valence) * 0.05f * dt_seconds;
  float pull_a = (0.0f - e->arousal) * 0.03f * dt_seconds;
  e->valence = clampf(e->valence + pull_v);
  e->arousal = clampf(e->arousal + pull_a);
  e->last_tick_ms = now_ms;
}

void emotion_react(emotion* e, float valence_delta, float arousal_delta) {
  if (!e) return;
  e->valence = clampf(e->valence + valence_delta);
  e->arousal = clampf(e->arousal + arousal_delta);
}

int emotion_anim_hint(const emotion* e) {
  if (e->arousal < -0.5f) return ANIM_SLEEP;
  if (e->valence > 0.4f) return ANIM_HAPPY;
  if (e->valence < -0.4f) return ANIM_SAD;
  return ANIM_IDLE;
}
