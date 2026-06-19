#include "persona.h"

void personality_defaults(personality* p) {
  p->mood_bias = 0.0f;
  p->sarcasm = 0.3f;
  p->curiosity = 0.6f;
  p->patience = 0.7f;
}
