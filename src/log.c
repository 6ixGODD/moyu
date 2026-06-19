#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static log_level g_level = LOG_INFO;
static FILE* g_file = NULL;

void log_set_level(log_level lvl) {
  g_level = lvl;
}

void log_set_file(const char* path) {
  if (g_file && g_file != stderr && g_file != stdout) fclose(g_file);
  if (path) {
    g_file = fopen(path, "a");
    if (!g_file) g_file = stderr;
  } else {
    g_file = NULL;
  }
}

static const char* lvl_str(log_level lvl) {
  switch (lvl) {
    case LOG_DEBUG: return "DEBUG";
    case LOG_INFO: return "INFO";
    case LOG_WARN: return "WARN";
    case LOG_ERROR: return "ERROR";
  }
  return "?";
}

void log_write(
    log_level lvl, const char* file, int line, const char* fmt, ...) {
  if (lvl < g_level) return;
  char buf[1024];
  int n = 0;
  time_t now = time(NULL);
  struct tm tmv;
#ifdef _WIN32
  localtime_s(&tmv, &now);
#else
  localtime_r(&now, &tmv);
#endif
  n += snprintf(buf + n,
                sizeof(buf) - n,
                "%04d-%02d-%02d %02d:%02d:%02d [%s] ",
                tmv.tm_year + 1900,
                tmv.tm_mon + 1,
                tmv.tm_mday,
                tmv.tm_hour,
                tmv.tm_min,
                tmv.tm_sec,
                lvl_str(lvl));
  const char* short_file = strrchr(file, '/');
  const char* short_file2 = strrchr(file, '\\');
  if (short_file2 > short_file) short_file = short_file2;
  if (!short_file)
    short_file = file;
  else
    short_file++;
  n += snprintf(buf + n, sizeof(buf) - n, "%s:%d ", short_file, line);
  va_list ap;
  va_start(ap, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
  va_end(ap);
  if (n >= (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 3;
  buf[n++] = '\n';
  buf[n] = 0;
  fputs(buf, stderr);
  fflush(stderr);
  if (g_file && g_file != stderr) {
    fputs(buf, g_file);
    fflush(g_file);
  }
}
