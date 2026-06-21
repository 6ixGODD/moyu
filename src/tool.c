#include "tool.h"

#include "mem.h"

#include <string.h>

void tool_registry_init(tool_registry* r, size_t cap) {
  r->defs = (tool_def*)moyu_alloc(cap * sizeof(tool_def));
  r->count = 0;
  r->cap = cap;
}

void tool_registry_free(tool_registry* r) {
  if (!r) return;
  for (size_t i = 0; i < r->count; i++) {
    moyu_free((void*)r->defs[i].name);
    moyu_free((void*)r->defs[i].description);
    moyu_free((void*)r->defs[i].input_schema_json);
    moyu_free((void*)r->defs[i].source);
    moyu_free((void*)r->defs[i].affordance);
  }
  if (r->defs) moyu_free(r->defs);
  r->defs = NULL;
  r->count = r->cap = 0;
}

void tool_registry_add(tool_registry* r, tool_def def) {
  if (r->count >= r->cap) {
    r->cap *= 2;
    r->defs = (tool_def*)moyu_realloc(r->defs, r->cap * sizeof(tool_def));
  }
  tool_def copy = def;
  copy.name = moyu_strdup(def.name ? def.name : "unnamed");
  copy.description = moyu_strdup(def.description ? def.description : "");
  copy.input_schema_json = moyu_strdup(def.input_schema_json ? def.input_schema_json : "{}");
  copy.source = moyu_strdup(def.source ? def.source : "builtin");
  copy.affordance = moyu_strdup(def.affordance ? def.affordance : "{}");
  r->defs[r->count++] = copy;
}

const char* tool_risk_name(tool_risk risk) {
  switch (risk) {
    case TOOL_DRAFT: return "draft";
    case TOOL_MUTATE: return "mutate";
    default: return "observe";
  }
}

const tool_def* tool_registry_find(const tool_registry* r, const char* name) {
  for (size_t i = 0; i < r->count; i++) {
    if (strcmp(r->defs[i].name, name) == 0) return &r->defs[i];
  }
  return NULL;
}

char* tool_invoke(const tool_def* def, const char* input_json) {
  if (!def || !def->invoke) return NULL;
  return def->invoke(input_json, def->user);
}
