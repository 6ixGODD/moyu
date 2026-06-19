#include "event.h"

#include "mem.h"
#include "platform.h"

#include <string.h>

void event_queue_init(event_queue* q, size_t cap) {
  q->cap = cap;
  q->buf = (internal_event*)moyu_alloc(cap * sizeof(internal_event));
  memset(q->buf, 0, cap * sizeof(internal_event));
  q->head = q->tail = q->count = 0;
}

static void ev_free_fields(internal_event* e) {
  if (e->str) {
    moyu_free(e->str);
    e->str = NULL;
  }
  if (e->str2) {
    moyu_free(e->str2);
    e->str2 = NULL;
  }
}

void event_queue_free(event_queue* q) {
  if (!q || !q->buf) return;
  for (size_t i = 0; i < q->cap; i++)
    ev_free_fields(&q->buf[i]);
  moyu_free(q->buf);
  q->buf = NULL;
  q->cap = q->head = q->tail = q->count = 0;
}

void event_queue_push(event_queue* q, internal_event e) {
  if (q->count >= q->cap) {
    // Drop oldest
    ev_free_fields(&q->buf[q->tail]);
    q->tail = (q->tail + 1) % q->cap;
    q->count--;
  }
  e.ts_ms = platform_now_ms();
  q->buf[q->head] = e;
  q->head = (q->head + 1) % q->cap;
  q->count++;
}

bool event_queue_pop(event_queue* q, internal_event* out) {
  if (q->count == 0) return false;
  *out = q->buf[q->tail];
  q->buf[q->tail].str = NULL;
  q->buf[q->tail].str2 = NULL;
  q->tail = (q->tail + 1) % q->cap;
  q->count--;
  return true;
}

void event_push_action(event_queue* q, const char* name, const char* payload) {
  internal_event e;
  memset(&e, 0, sizeof(e));
  e.kind = EV_LUA_ACTION;
  e.str = name ? moyu_strdup(name) : NULL;
  e.str2 = payload ? moyu_strdup(payload) : NULL;
  event_queue_push(q, e);
}

void event_push_say(event_queue* q, const char* text) {
  internal_event e;
  memset(&e, 0, sizeof(e));
  e.kind = EV_SAY;
  e.str = text ? moyu_strdup(text) : NULL;
  event_queue_push(q, e);
}

void event_push_anim(event_queue* q, int anim_id) {
  internal_event e;
  memset(&e, 0, sizeof(e));
  e.kind = EV_ANIM_REQUEST;
  e.payload.i = anim_id;
  event_queue_push(q, e);
}

void event_push_llm_result(event_queue* q, const char* text) {
  internal_event e;
  memset(&e, 0, sizeof(e));
  e.kind = EV_LLM_RESULT;
  e.str = text ? moyu_strdup(text) : NULL;
  event_queue_push(q, e);
}
