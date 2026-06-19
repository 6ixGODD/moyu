#pragma once
#include <stddef.h>
#include <stdint.h>

// Tiny LRU cache mapping request hash -> response text.
// LLM responses are stateless in our model, so identical prompts can be cached.

typedef struct llm_cache_entry {
    uint64_t key;
    char*    value;       // owned
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
void  llm_cache_put(llm_cache* c, uint64_t key, const char* value);
