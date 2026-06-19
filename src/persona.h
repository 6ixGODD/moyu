#pragma once

typedef struct {
  float mood_bias;  // -1..1, baseline mood offset
  float sarcasm;    // 0..1
  float curiosity;  // 0..1
  float patience;   // 0..1
} personality;

void personality_defaults(personality* p);
