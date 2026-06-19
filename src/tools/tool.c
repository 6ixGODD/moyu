#include "tool.h"
#include "../util/mem.h"

#include <string.h>

void tool_registry_init(tool_registry* r, size_t cap) {
    r->defs = (tool_def*)moyu_alloc(cap * sizeof(tool_def));
    r->count = 0;
    r->cap = cap;
}

void tool_registry_free(tool_registry* r) {
    if (!r) return;
    if (r->defs) moyu_free(r->defs);
    r->defs = NULL;
    r->count = r->cap = 0;
}

void tool_registry_add(tool_registry* r, tool_def def) {
    if (r->count >= r->cap) {
        r->cap *= 2;
        r->defs = (tool_def*)moyu_realloc(r->defs, r->cap * sizeof(tool_def));
    }
    r->defs[r->count++] = def;
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
