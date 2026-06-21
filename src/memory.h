#pragma once

#include "state.h"
#include "workdir.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct memory_system {
  moyu_workdir* wd;
  state_store* state;
  char* soul;
  char* memory;
  uint64_t loaded_at_ms;
} memory_system;

bool memory_init(memory_system* m, moyu_workdir* wd, state_store* state);
void memory_free(memory_system* m);
bool memory_reload(memory_system* m);
bool memory_remember(memory_system* m, const char* section, const char* text);
int memory_forget(memory_system* m, const char* needle);
bool memory_consider_episode(memory_system* m,
                             int64_t episode_id,
                             const char* summary,
                             double importance,
                             double novelty,
                             double personality_bias);
char* memory_compose(memory_system* m,
                     const char* active_intention,
                     const char* recent_context,
                     const char* current_input,
                     size_t max_chars);
