#include "mem.h"

#include <stdlib.h>
#include <string.h>

void* moyu_alloc(size_t n) {
  void* p = malloc(n);
  if (!p) abort();
  return p;
}

void* moyu_realloc(void* p, size_t n) {
  void* q = realloc(p, n);
  if (!q && n > 0) abort();
  return q;
}

void moyu_free(void* p) {
  free(p);
}

char* moyu_strdup(const char* s) {
  size_t n = strlen(s) + 1;
  char* p = (char*)moyu_alloc(n);
  memcpy(p, s, n);
  return p;
}

char* moyu_strndup(const char* s, size_t n) {
  size_t len = strnlen(s, n);
  char* p = (char*)moyu_alloc(len + 1);
  memcpy(p, s, len);
  p[len] = 0;
  return p;
}
