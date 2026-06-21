#pragma once

#include <stdbool.h>

bool secrets_store(const char* path, const char* secret);
char* secrets_load(const char* path);

