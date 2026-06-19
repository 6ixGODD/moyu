#include "hash.h"

uint64_t hash_fnv1a64(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

uint64_t hash_fnv1a64_str(const char* s) {
  uint64_t h = 0xcbf29ce484222325ULL;
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 0x100000001b3ULL;
  }
  return h;
}

uint64_t hash_fnv1a64_combine(uint64_t a, uint64_t b) {
  uint64_t h = a;
  h ^= (b & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 8) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 16) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 24) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 32) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 40) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 48) & 0xff);
  h *= 0x100000001b3ULL;
  h ^= ((b >> 56) & 0xff);
  h *= 0x100000001b3ULL;
  return h;
}
