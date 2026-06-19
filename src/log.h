#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  LOG_DEBUG = 0,
  LOG_INFO,
  LOG_WARN,
  LOG_ERROR,
} log_level;

void log_set_level(log_level lvl);
void log_set_file(const char* path);  // NULL = stderr only
void log_write(log_level lvl, const char* file, int line, const char* fmt, ...);

#define LOGD(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOGI(...) log_write(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) log_write(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
