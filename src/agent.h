#pragma once

#include <stdbool.h>
#include <stdint.h>

struct moyu_app;

typedef struct agent_runtime {
  struct moyu_app* app;
  int64_t active_id;
  char goal[256];
  char status[32];
  int step_index;
  uint64_t next_autonomous_ms;
  uint64_t rng;
  bool autonomous_enabled;
} agent_runtime;

bool agent_init(agent_runtime* a, struct moyu_app* app);
void agent_on_human_event(agent_runtime* a, const char* kind, const char* detail);
void agent_tick(agent_runtime* a, uint64_t now_ms, float idle_seconds);
void agent_cancel(agent_runtime* a, const char* reason);
char* agent_explain(agent_runtime* a);

