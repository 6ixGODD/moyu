#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Internal event queue. Decoupled from platform events: platform events are
// translated into these and pushed; the loop drains them on each iteration.

typedef enum {
  EV_NONE = 0,
  EV_MOUSE_NEAR,  // payload.i = distance
  EV_MOUSE_CLICK,
  EV_IDLE_TICK,  // payload.f = idle_seconds
  EV_LUA_ACTION,  // payload.str = action name (owned), payload.str2 = payload (owned)
  EV_LLM_RESULT,  // payload.str = response (owned)
  EV_TOOL_RESULT,  // payload.str = tool name (owned), payload.str2 = result (owned)
  EV_ANIM_REQUEST,  // payload.i = anim_id
  EV_SAY,           // payload.str = text (owned)
  EV_QUIT,
} event_kind;

typedef struct {
  event_kind kind;
  union {
    int i;
    float f;
  } payload;
  char* str;   // owned (may be NULL)
  char* str2;  // owned (may be NULL)
  uint64_t ts_ms;
} internal_event;

typedef struct {
  internal_event* buf;
  size_t cap, head, tail, count;
} event_queue;

void event_queue_init(event_queue* q, size_t cap);
void event_queue_free(event_queue* q);
void event_queue_push(event_queue* q, internal_event e);
bool event_queue_pop(event_queue* q, internal_event* out);

// Convenience: push an action with strings (copied).
void event_push_action(event_queue* q, const char* name, const char* payload);
void event_push_say(event_queue* q, const char* text);
void event_push_anim(event_queue* q, int anim_id);
void event_push_llm_result(event_queue* q, const char* text);
