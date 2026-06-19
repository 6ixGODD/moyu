#pragma once
#include "personality.h"
#include <stdint.h>

// Emotion is a 2D Russell circumplex approximation:
//   valence  ∈ [-1, 1]  (negative = sad, positive = happy)
//   arousal  ∈ [-1, 1]  (low = calm, high = excited)
// It drifts slowly over time via a biased random walk — non-deterministic.

typedef struct {
    float valence;
    float arousal;
    uint64_t last_tick_ms;
    uint64_t rng_state;
} emotion;

void emotion_init(emotion* e, const personality* p);
// Advance emotion by dt_ms. personality biases the drift.
void emotion_tick(emotion* e, const personality* p, uint64_t now_ms, float dt_seconds);

// Returns an animation id hint based on current emotion (caller may override).
int emotion_anim_hint(const emotion* e);
