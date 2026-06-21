// platform_linux.c — X11 + libcurl implementation (stub for MVP).
// Compiles so the project builds on Linux, but the runtime will report
// "not implemented" and exit. Full X11 layered-window equivalent + libcurl
// POST can be added without touching the rest of the codebase.

#include "log.h"
#include "mem.h"
#include "platform.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

uint64_t platform_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}
uint64_t platform_unix_ms(void) { return platform_now_ms(); }

const char* platform_home_dir(void) {
  const char* v = getenv("MOYU_HOME");
  if (v && *v) return v;
  v = getenv("HOME");
  return (v && *v) ? v : ".";
}

void platform_sleep_ms(uint32_t ms) {
  struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

static char g_exe_dir[4096] = {0};
const char* platform_exe_dir(void) {
  if (g_exe_dir[0]) return g_exe_dir;
  ssize_t n = readlink("/proc/self/exe", g_exe_dir, sizeof(g_exe_dir) - 1);
  if (n <= 0) {
    g_exe_dir[0] = '.';
    g_exe_dir[1] = 0;
    return g_exe_dir;
  }
  g_exe_dir[n] = 0;
  char* slash = strrchr(g_exe_dir, '/');
  if (slash) *slash = 0;
  return g_exe_dir;
}

char* platform_join_path(const char* dir, const char* name) {
  size_t a = strlen(dir), b = strlen(name);
  char* p = (char*)moyu_alloc(a + 1 + b + 1);
  memcpy(p, dir, a);
  p[a] = '/';
  memcpy(p + a + 1, name, b + 1);
  return p;
}

char* platform_read_file(const char* path, size_t* out_len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  char* buf = (char*)moyu_alloc((size_t)sz + 1);
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = 0;
  if (out_len) *out_len = rd;
  return buf;
}

bool platform_write_file(const char* path, const void* data, size_t len) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  size_t wr = fwrite(data, 1, len, f);
  fclose(f);
  return wr == len;
}
bool platform_file_exists(const char* path) { return access(path, F_OK) == 0; }
bool platform_remove_file(const char* path) { return unlink(path) == 0; }
bool platform_move_file(const char* from,const char* to,bool replace){(void)replace;return rename(from,to)==0;}
bool platform_make_dirs(const char* path) {
  char buf[4096];
  if (strlen(path) >= sizeof(buf)) return false;
  strcpy(buf, path);
  for (char* p = buf + 1; *p; p++) {
    if (*p == '/') { *p = 0; mkdir(buf, 0700); *p = '/'; }
  }
  return mkdir(buf, 0700) == 0 || errno == EEXIST;
}
bool platform_write_file_atomic(const char* path, const void* data, size_t len) {
  char tmp[4096];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  if (!platform_write_file(tmp, data, len)) return false;
  return rename(tmp, path) == 0;
}

void platform_get_cursor_pos(int* x, int* y) {
  if (x) *x = 0;
  if (y) *y = 0;
  LOGW("platform_get_cursor_pos not implemented on Linux stub");
}

struct platform_window {
  int dummy;
};

platform_window* platform_window_create(int w,
                                        int h,
                                        int x,
                                        int y,
                                        bool transparent,
                                        bool topmost,
                                        bool click_through) {
  (void)w;
  (void)h;
  (void)x;
  (void)y;
  (void)transparent;
  (void)topmost;
  (void)click_through;
  LOGE("platform_window_create not implemented on Linux stub");
  return NULL;
}

void platform_window_destroy(platform_window* w) {
  (void)w;
}
void platform_window_set_pixels(platform_window* w,
                                const uint32_t* rgba,
                                int ww,
                                int hh) {
  (void)w;
  (void)rgba;
  (void)ww;
  (void)hh;
}
void platform_window_move(platform_window* w, int x, int y) {
  (void)w;
  (void)x;
  (void)y;
}
void platform_window_get_pos(platform_window* w, int* x, int* y) {
  (void)w;
  (void)x;
  (void)y;
}
void platform_window_show(platform_window* w) {
  (void)w;
}
void platform_window_hide(platform_window* w) {
  (void)w;
}
void platform_window_set_clickable(
    platform_window* w, int x, int y, int w_, int h_) {
  (void)w;
  (void)x;
  (void)y;
  (void)w_;
  (void)h_;
}
void platform_window_wake(platform_window* w) { (void)w; }

bool platform_poll_event(platform_window* w,
                         platform_event* out,
                         int timeout_ms) {
  (void)w;
  (void)timeout_ms;
  memset(out, 0, sizeof(*out));
  out->type = PE_QUIT;
  return true;
}

platform_http_resp platform_http_post_json(const char* url,
                                           const char* auth_bearer,
                                           const char* json_body,
                                           int timeout_ms) {
  (void)url;
  (void)auth_bearer;
  (void)json_body;
  (void)timeout_ms;
  platform_http_resp r = {0};
  r.err = moyu_strdup("Linux HTTP stub: link libcurl to enable");
  return r;
}
platform_http_resp platform_http_request(const char* method,const char* url,const char* auth,const char* headers,const char* body,const char* content_type,int timeout_ms){(void)method;(void)headers;(void)content_type;return platform_http_post_json(url,auth,body?body:"",timeout_ms);}

void platform_http_resp_free(platform_http_resp* r) {
  if (!r) return;
  if (r->body) moyu_free(r->body);
  if (r->err) moyu_free(r->err);
  if (r->content_type) moyu_free(r->content_type);
  if (r->session_id) moyu_free(r->session_id);
  r->body = NULL;
  r->err = NULL;
  r->body_len = 0;
  r->status = 0;
  r->content_type = r->session_id = NULL;
}

uint8_t* platform_get_glyph(uint32_t codepoint,
                            int pixel_size,
                            int* w,
                            int* h) {
  (void)codepoint;
  (void)pixel_size;
  if (w) *w = 0;
  if (h) *h = 0;
  return NULL;  // CJK not supported on Linux stub yet
}
