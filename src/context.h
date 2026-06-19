#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Context System — the only state representation of the agent.
// Each node = one piece of evolved context. Stored in a rolling ring buffer.

typedef enum {
  CTX_IDLE = 0,
  CTX_INTERACTION,
  CTX_TOOL,
  CTX_REFLECTION,
} context_type;

typedef struct {
  uint64_t id;
  context_type type;
  char* input;     // JSON string (owned)
  char* output;    // JSON string (owned, may be NULL)
  char* metadata;  // JSON string (owned, may be NULL)
  uint64_t ts_ms;
} context_node;

typedef struct {
  context_node* ring;
  size_t cap;    // max nodes kept
  size_t head;   // next write index
  size_t count;  // current count
  uint64_t next_id;
} context_store;

void context_store_init(context_store* s, size_t cap);
void context_store_free(context_store* s);

// Push a new node. Strings are heap-copied; NULL allowed for output/metadata.
void context_push(context_store* s,
                  context_type type,
                  const char* input,
                  const char* output,
                  const char* metadata);

// Get the most-recent node of a given type, or NULL.
const context_node* context_last_of(const context_store* s, context_type type);

// Iterate recent nodes in reverse chronological order (newest first).
// Returns NULL when iteration ends.
const context_node* context_at(const context_store* s, size_t reverse_index);

// Compute a 64-bit hash of the rolling window (for LLM cache key).
uint64_t context_hash(const context_store* s);

// Compose a flat JSON array describing the last N nodes — for sending to LLM.
// Returned string is heap-allocated; caller frees.
char* context_to_json(const context_store* s, size_t last_n);
