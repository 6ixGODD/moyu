#pragma once

#include <stdbool.h>
#include <stdint.h>

struct llm_config;
struct platform_window;

typedef enum {
  ASYNC_NONE = 0,
  ASYNC_LLM,
} async_kind;

typedef struct {
  async_kind kind;
  uint64_t id;
  char* purpose;
  char* text;
  char* error;
  int status;
} async_result;

typedef struct async_worker async_worker;

async_worker* async_worker_create(struct llm_config* llm,
                                  struct platform_window* wake_window);
void async_worker_destroy(async_worker* w);
bool async_submit_llm(async_worker* w,
                      const char* purpose,
                      const char* system,
                      const char* user,
                      int timeout_ms,
                      uint64_t* out_id);
bool async_poll(async_worker* w, async_result* out);
void async_result_free(async_result* r);
bool async_busy(async_worker* w);

