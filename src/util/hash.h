#pragma once
#include <stdint.h>
#include <stddef.h>

uint64_t hash_fnv1a64(const void* data, size_t len);
uint64_t hash_fnv1a64_str(const char* s);
uint64_t hash_fnv1a64_combine(uint64_t a, uint64_t b);
