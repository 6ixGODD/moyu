#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct llm_config {
  char* base_url;  // e.g. "https://api.deepseek.com/v1"  (owned)
  char* api_key;   // owned
  char* model;     // owned
  int max_tokens;
  float temperature;
  bool json_mode;
} llm_config;

void llm_config_init(llm_config* c);
void llm_config_free(llm_config* c);

typedef struct {
  char* text;   // owned; assistant message content
  char* error;  // owned; non-NULL on failure
  int status;   // HTTP status
} llm_result;

void llm_result_free(llm_result* r);

// messages: alternating "user"/"assistant" messages as plain strings.
// First message should typically be the system prompt; we send it as role=system.
// Subsequent messages alternate user/assistant starting from user.
// The function builds the OpenAI-compatible request body and calls the platform HTTP layer.
llm_result llm_complete(llm_config* cfg,
                        const char* unused_system_separate,
                        const char** messages,
                        size_t n,
                        int timeout_ms);
#include <stddef.h>
#include <stdint.h>

// Tiny LRU cache mapping request hash -> response text.
// LLM responses are stateless in our model, so identical prompts can be cached.

typedef struct llm_cache_entry {
  uint64_t key;
  char* value;  // owned
  uint64_t ts_ms;
  struct llm_cache_entry* prev;
  struct llm_cache_entry* next;
} llm_cache_entry;

typedef struct llm_cache {
  llm_cache_entry* head;  // most recent
  llm_cache_entry* tail;  // least recent
  size_t count;
  size_t cap;
} llm_cache;

void llm_cache_init(llm_cache* c, size_t cap);
void llm_cache_free(llm_cache* c);

// Returns the cached value (owned copy) or NULL.
char* llm_cache_get(llm_cache* c, uint64_t key);
// Stores a copy of value under key (evicts LRU if full).
void llm_cache_put(llm_cache* c, uint64_t key, const char* value);
