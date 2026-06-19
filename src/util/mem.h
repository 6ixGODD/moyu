#pragma once
#include <stddef.h>

// Thin wrappers around malloc/realloc/free. No custom allocator in MVP —
// we keep these for future instrumentation / Lua allocator hooks.

void* moyu_alloc(size_t n);
void* moyu_realloc(void* p, size_t n);
void  moyu_free(void* p);

char* moyu_strdup(const char* s);
char* moyu_strndup(const char* s, size_t n);
