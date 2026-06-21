#include "desktop_os.h"

#include "mem.h"
#include "platform.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

static bool has_ext(const char* path, const char* ext) {
  size_t lp = strlen(path), le = strlen(ext);
  if (lp < le) return false;
#ifdef _WIN32
  return _stricmp(path + lp - le, ext) == 0;
#else
  return strcasecmp(path + lp - le, ext) == 0;
#endif
}

static bool is_text_ext(const char* path) {
  static const char* exts[] = {
      ".txt",".md",".json",".yaml",".yml",".toml",".ini",".log",".csv",".c",".h",".cpp",".hpp",".cc",".py",".js",".ts",".tsx",".jsx",".java",".go",".rs",".lua",".xml",".html",".css",".sql",NULL};
  for (int i = 0; exts[i]; i++) if (has_ext(path, exts[i])) return true;
  return false;
}

static bool is_image_ext(const char* path) {
  static const char* exts[] = {".png",".jpg",".jpeg",".webp",".bmp",".gif",NULL};
  for (int i = 0; exts[i]; i++) if (has_ext(path, exts[i])) return true;
  return false;
}

static bool is_video_ext(const char* path) {
  static const char* exts[] = {".mp4",".mov",".avi",".mkv",".webm",NULL};
  for (int i = 0; exts[i]; i++) if (has_ext(path, exts[i])) return true;
  return false;
}

bool desktop_os_collect_system_snapshot(t_system_snapshot* out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
  out->epoch_ms = platform_unix_ms();
#ifdef _WIN32
  MEMORYSTATUSEX ms;
  memset(&ms, 0, sizeof(ms));
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) out->memory_percent = (double)ms.dwMemoryLoad;

  FILETIME idle = {0}, kernel = {0}, user = {0};
  static ULARGE_INTEGER prev_kernel = {0}, prev_user = {0};
  static uint64_t prev_tick = 0;
  if (GetSystemTimes(&idle, &kernel, &user)) {
    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    uint64_t now = platform_now_ms();
    if (prev_tick) {
      unsigned long long dk = k.QuadPart - prev_kernel.QuadPart;
      unsigned long long du = u.QuadPart - prev_user.QuadPart;
      unsigned long long total = dk + du;
      uint64_t dt = now - prev_tick;
      if (dt) out->cpu_percent = (double)total / (double)(dt * 10000.0);
      if (out->cpu_percent < 0) out->cpu_percent = 0;
      if (out->cpu_percent > 100) out->cpu_percent = 100;
    }
    prev_kernel = k;
    prev_user = u;
    prev_tick = now;
  }

  DWORD hn = sizeof(out->host);
  GetComputerNameA(out->host, &hn);

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
    char hostbuf[128] = {0};
    if (!gethostname(hostbuf, (int)sizeof(hostbuf))) {
      struct addrinfo hints, *res = NULL;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      if (getaddrinfo(hostbuf, NULL, &hints, &res) == 0 && res) {
        struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
        inet_ntop(AF_INET, &addr->sin_addr, out->ip, sizeof(out->ip));
        freeaddrinfo(res);
      }
    }
    WSACleanup();
  }
#endif
  time_t t = (time_t)(out->epoch_ms / 1000ULL);
  struct tm tmv;
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  strftime(out->local_time, sizeof(out->local_time), "%Y-%m-%d %H:%M:%S", &tmv);
  return true;
}

char* desktop_os_format_system_snapshot(const t_system_snapshot* s) {
  if (!s) return moyu_strdup("{}");
  char buf[512];
  snprintf(buf,
           sizeof(buf),
           "{\"time\":\"%s\",\"cpu_percent\":%.1f,\"memory_percent\":%.1f,\"host\":\"%s\",\"ip\":\"%s\"}",
           s->local_time,
           s->cpu_percent,
           s->memory_percent,
           s->host,
           s->ip);
  return moyu_strdup(buf);
}

void desktop_os_default_owner_profile(t_owner_profile* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
#ifdef _WIN32
  DWORD n = sizeof(out->name);
  GetEnvironmentVariableA("USERNAME", out->name, n);
#endif
  if (!out->name[0]) snprintf(out->name, sizeof(out->name), "human");
}

bool desktop_os_preview_path(const char* path, t_path_preview* out) {
  if (!path || !out) return false;
  memset(out, 0, sizeof(*out));
  snprintf(out->path, sizeof(out->path), "%s", path);
  const char* name = strrchr(path, '\\');
  if (!name) name = strrchr(path, '/');
  name = name ? name + 1 : path;
  snprintf(out->title, sizeof(out->title), "%s", name);
  out->is_textual = is_text_ext(path);
  out->is_image = is_image_ext(path);
  out->is_video = is_video_ext(path);
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES) return false;
  out->is_directory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if (out->is_directory) {
    snprintf(out->kind, sizeof(out->kind), "directory");
    return true;
  }
  HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  memset(&sz, 0, sizeof(sz));
  GetFileSizeEx(h, &sz);
  out->size_bytes = (uint64_t)sz.QuadPart;
  if (out->is_textual) {
    DWORD want = (DWORD)(out->size_bytes > 3000 ? 3000 : out->size_bytes);
    if (want > sizeof(out->excerpt) - 1) want = sizeof(out->excerpt) - 1;
    DWORD got = 0;
    ReadFile(h, out->excerpt, want, &got, NULL);
    out->excerpt[got] = 0;
    snprintf(out->kind, sizeof(out->kind), "text");
  } else if (out->is_image) {
    snprintf(out->kind, sizeof(out->kind), "image");
  } else if (out->is_video) {
    snprintf(out->kind, sizeof(out->kind), "video");
  } else {
    snprintf(out->kind, sizeof(out->kind), "file");
  }
  CloseHandle(h);
  return true;
#else
  return false;
#endif
}
