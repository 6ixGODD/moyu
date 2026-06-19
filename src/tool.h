#pragma once
#include <stdbool.h>
#include <stddef.h>

// Lightweight tool dispatcher. NOT a framework — just a registry of name→function.

typedef char* (*tool_fn)(const char* input_json, void* user);

typedef struct {
  const char* name;               // static string (not owned)
  const char* description;        // static string (not owned)
  const char* input_schema_json;  // static JSON schema string (not owned)
  tool_fn invoke;
  void* user;  // passed back to invoke
} tool_def;

typedef struct tool_registry {
  tool_def* defs;
  size_t count, cap;
} tool_registry;

void tool_registry_init(tool_registry* r, size_t cap);
void tool_registry_free(tool_registry* r);
void tool_registry_add(tool_registry* r, tool_def def);
const tool_def* tool_registry_find(const tool_registry* r, const char* name);

// Synchronous invoke. Returns owned result JSON string (caller frees) or NULL.
char* tool_invoke(const tool_def* def, const char* input_json);
