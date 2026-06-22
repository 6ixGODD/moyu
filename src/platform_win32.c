#include "log.h"
#include "mem.h"
#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// ---------- Time ----------
uint64_t platform_now_ms(void) {
  static LARGE_INTEGER freq = {0};
  if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER c;
  QueryPerformanceCounter(&c);
  return (uint64_t)(c.QuadPart * 1000ULL / freq.QuadPart);
}

uint64_t platform_unix_ms(void) {
  FILETIME ft;
  ULARGE_INTEGER t;
  GetSystemTimeAsFileTime(&ft);
  t.LowPart = ft.dwLowDateTime;
  t.HighPart = ft.dwHighDateTime;
  return (t.QuadPart - 116444736000000000ULL) / 10000ULL;
}

void platform_sleep_ms(uint32_t ms) {
  Sleep(ms);
}

// ---------- Filesystem ----------
static char g_exe_dir[MAX_PATH] = {0};
static char g_home_dir[MAX_PATH * 4] = {0};

static bool utf8_to_wide(const char* s, wchar_t* out, size_t cap) {
  if (!s || !out || cap == 0) return false;
  return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap) > 0;
}

const char* platform_home_dir(void) {
  if (g_home_dir[0]) return g_home_dir;
  wchar_t wbuf[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"MOYU_HOME", wbuf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH)
    n = GetEnvironmentVariableW(L"USERPROFILE", wbuf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) wcscpy_s(wbuf, MAX_PATH, L".");
  WideCharToMultiByte(CP_UTF8,
                      0,
                      wbuf,
                      -1,
                      g_home_dir,
                      (int)sizeof(g_home_dir),
                      NULL,
                      NULL);
  return g_home_dir;
}
const char* platform_exe_dir(void) {
  if (g_exe_dir[0]) return g_exe_dir;
  DWORD n = GetModuleFileNameW(NULL, (wchar_t*)g_exe_dir, MAX_PATH);
  (void)n;
  // convert wide to narrow in place — cheap; we stored wide. Do it properly:
  wchar_t wpath[MAX_PATH];
  GetModuleFileNameW(NULL, wpath, MAX_PATH);
  WideCharToMultiByte(CP_UTF8, 0, wpath, -1, g_exe_dir, MAX_PATH, NULL, NULL);
  char* slash = strrchr(g_exe_dir, '\\');
  if (slash) *slash = 0;
  return g_exe_dir;
}

char* platform_join_path(const char* dir, const char* name) {
  size_t a = strlen(dir), b = strlen(name);
  char* p = (char*)moyu_alloc(a + 1 + b + 1);
  memcpy(p, dir, a);
  p[a] = '\\';
  memcpy(p + a + 1, name, b + 1);
  return p;
}

char* platform_read_file(const char* path, size_t* out_len) {
  FILE* f = NULL;
  if (fopen_s(&f, path, "rb") != 0 || !f) return NULL;
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
  FILE* f = NULL;
  if (fopen_s(&f, path, "wb") != 0 || !f) return false;
  size_t wr = fwrite(data, 1, len, f);
  fclose(f);
  return wr == len;
}

bool platform_file_exists(const char* path) {
  wchar_t wpath[32768];
  if (!utf8_to_wide(path, wpath, 32768)) return false;
  DWORD attrs = GetFileAttributesW(wpath);
  return attrs != INVALID_FILE_ATTRIBUTES;
}

bool platform_make_dirs(const char* path) {
  wchar_t wpath[32768];
  if (!utf8_to_wide(path, wpath, 32768)) return false;
  size_t n = wcslen(wpath);
  for (size_t i = 1; i < n; i++) {
    if (wpath[i] != L'\\' && wpath[i] != L'/') continue;
    wchar_t saved = wpath[i];
    wpath[i] = 0;
    if (!(i == 2 && wpath[1] == L':')) CreateDirectoryW(wpath, NULL);
    wpath[i] = saved;
  }
  if (CreateDirectoryW(wpath, NULL)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

bool platform_remove_file(const char* path) {
  wchar_t wpath[32768];
  return utf8_to_wide(path, wpath, 32768) && DeleteFileW(wpath);
}

bool platform_move_file(const char* from, const char* to, bool replace) {
  wchar_t wf[32768], wt[32768];
  if (!utf8_to_wide(from, wf, 32768) || !utf8_to_wide(to, wt, 32768))
    return false;
  DWORD flags = MOVEFILE_WRITE_THROUGH;
  if (replace) flags |= MOVEFILE_REPLACE_EXISTING;
  return MoveFileExW(wf, wt, flags) != 0;
}

bool platform_write_file_atomic(const char* path, const void* data, size_t len) {
  size_t n = strlen(path);
  char* tmp = (char*)moyu_alloc(n + 5);
  char* bak = (char*)moyu_alloc(n + 5);
  snprintf(tmp, n + 5, "%s.tmp", path);
  snprintf(bak, n + 5, "%s.bak", path);
  bool ok = platform_write_file(tmp, data, len);
  wchar_t wpath[32768], wtmp[32768], wbak[32768];
  if (ok && utf8_to_wide(path, wpath, 32768) &&
      utf8_to_wide(tmp, wtmp, 32768) && utf8_to_wide(bak, wbak, 32768)) {
    if (platform_file_exists(path))
      CopyFileW(wpath, wbak, FALSE);
    ok = MoveFileExW(wtmp,
                     wpath,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
  } else {
    ok = false;
  }
  if (!ok) platform_remove_file(tmp);
  moyu_free(tmp);
  moyu_free(bak);
  return ok;
}

void platform_get_cursor_pos(int* x, int* y) {
  POINT p;
  GetCursorPos(&p);
  if (x) *x = p.x;
  if (y) *y = p.y;
}

void platform_get_work_area_at(int x, int y, int* left, int* top,
                               int* width, int* height) {
  POINT point = {x, y};
  HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info = {sizeof(info)};
  if (!GetMonitorInfoW(monitor, &info)) {
    info.rcWork.left = 0;
    info.rcWork.top = 0;
    info.rcWork.right = GetSystemMetrics(SM_CXSCREEN);
    info.rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
  }
  if (left) *left = info.rcWork.left;
  if (top) *top = info.rcWork.top;
  if (width) *width = info.rcWork.right - info.rcWork.left;
  if (height) *height = info.rcWork.bottom - info.rcWork.top;
}

// ---------- Window ----------
struct platform_window {
  HWND hwnd;
  HDC mem_dc;
  HBITMAP dib;
  uint32_t* bits;
  int w, h;
  bool click_through;
  int last_hit_x, last_hit_y;
  bool hit_valid;
  int hit_alpha;  // alpha value at last hit-test point
  // Clickable sub-rect (window-relative). Default = whole window.
  bool has_clickable;
  int cx, cy, cw, ch;
};

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  platform_window* pw = (platform_window*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (msg) {
    case WM_NCHITTEST: {
      if (!pw) break;
      POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(h, &pt);
      if (pt.x < 0 || pt.y < 0 || pt.x >= pw->w || pt.y >= pw->h)
        return HTTRANSPARENT;
      // If a clickable rect is set, anything outside it passes through.
      if (pw->has_clickable) {
        if (pt.x < pw->cx || pt.y < pw->cy || pt.x >= pw->cx + pw->cw ||
            pt.y >= pw->cy + pw->ch)
          return HTTRANSPARENT;
      }
      uint32_t px = pw->bits[pt.y * pw->w + pt.x];
      int alpha = (px >> 24) & 0xff;
      if (alpha < 16) return HTTRANSPARENT;
      return HTCLIENT;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_QUIT: break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

static const wchar_t* WCLASS = L"moyu_pet_window";
#define WM_MOYU_WAKE (WM_APP + 42)

static void register_class(void) {
  static bool done = false;
  if (done) return;
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = WCLASS;
  RegisterClassExW(&wc);
  done = true;
}

platform_window* platform_window_create(int w,
                                        int h,
                                        int x,
                                        int y,
                                        bool transparent,
                                        bool topmost,
                                        bool click_through) {
  register_class();
  platform_window* pw = (platform_window*)moyu_alloc(sizeof(*pw));
  memset(pw, 0, sizeof(*pw));
  pw->w = w;
  pw->h = h;
  pw->click_through = click_through;

  DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW;
  if (topmost) ex |= WS_EX_TOPMOST;
  if (click_through) ex |= WS_EX_TRANSPARENT;
  (void)transparent;

  pw->hwnd = CreateWindowExW(ex,
                             WCLASS,
                             L"moyu",
                             WS_POPUP,
                             x,
                             y,
                             w,
                             h,
                             NULL,
                             NULL,
                             GetModuleHandleW(NULL),
                             NULL);
  if (!pw->hwnd) {
    LOGE("CreateWindowExW failed: %lu", GetLastError());
    moyu_free(pw);
    return NULL;
  }
  SetWindowLongPtrW(pw->hwnd, GWLP_USERDATA, (LONG_PTR)pw);
  DragAcceptFiles(pw->hwnd, TRUE);

  // 32-bit DIB section for alpha-blended blit
  BITMAPINFO bi = {0};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen_dc = GetDC(NULL);
  pw->dib = CreateDIBSection(
      screen_dc, &bi, DIB_RGB_COLORS, (void**)&pw->bits, NULL, 0);
  pw->mem_dc = CreateCompatibleDC(screen_dc);
  SelectObject(pw->mem_dc, pw->dib);
  ReleaseDC(NULL, screen_dc);
  // Start fully transparent
  memset(pw->bits, 0, (size_t)w * h * 4);

  return pw;
}

void platform_window_destroy(platform_window* w) {
  if (!w) return;
  if (w->mem_dc) DeleteDC(w->mem_dc);
  if (w->dib) DeleteObject(w->dib);
  if (w->hwnd) DestroyWindow(w->hwnd);
  moyu_free(w);
}

void platform_window_set_pixels(platform_window* w,
                                const uint32_t* rgba,
                                int ww,
                                int hh) {
  if (!w || !rgba) return;
  if (ww != w->w || hh != w->h) return;

  // Convert engine format RGBA (R high, A low) -> premultiplied BGRA, which
  // is what a 32-bit BI_RGB DIB stores and UpdateLayeredWindow/ULW_ALPHA
  // expects (alpha in the high byte, channels premultiplied). Without this,
  // opaque body pixels land as semi-transparent ghosts and R/B are swapped.
  int n = ww * hh;
  for (int i = 0; i < n; i++) {
    uint32_t p = rgba[i];
    uint8_t a = (uint8_t)(p & 0xff);
    uint8_t r = (uint8_t)((p >> 24) & 0xff);
    uint8_t g = (uint8_t)((p >> 16) & 0xff);
    uint8_t b = (uint8_t)((p >> 8) & 0xff);
    // Premultiply by alpha (no-op for a==0 or a==255, which is all we use).
    r = (uint8_t)((r * a) / 255);
    g = (uint8_t)((g * a) / 255);
    b = (uint8_t)((b * a) / 255);
    w->bits[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                 ((uint32_t)g << 8) | b;
  }

  POINT zero = {0, 0};
  SIZE size = {w->w, w->h};
  BLENDFUNCTION bf = {0};
  bf.BlendOp = AC_SRC_OVER;
  bf.SourceConstantAlpha = 255;
  bf.AlphaFormat = AC_SRC_ALPHA;
  POINT pos = {0, 0};
  RECT rc;
  GetWindowRect(w->hwnd, &rc);
  pos.x = rc.left;
  pos.y = rc.top;
  UpdateLayeredWindow(
      w->hwnd, NULL, &pos, &size, w->mem_dc, &zero, 0, &bf, ULW_ALPHA);
}

void platform_window_move(platform_window* w, int x, int y) {
  if (!w) return;
  SetWindowPos(
      w->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void platform_window_get_pos(platform_window* w, int* x, int* y) {
  if (!w) return;
  RECT rc;
  GetWindowRect(w->hwnd, &rc);
  if (x) *x = rc.left;
  if (y) *y = rc.top;
}

void platform_window_show(platform_window* w) {
  if (!w) return;
  ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
}

void platform_window_hide(platform_window* w) {
  if (!w) return;
  ShowWindow(w->hwnd, SW_HIDE);
}

void platform_window_set_clickable(
    platform_window* w, int x, int y, int w_, int h_) {
  if (!w) return;
  w->has_clickable = true;
  w->cx = x;
  w->cy = y;
  w->cw = w_;
  w->ch = h_;
}

void platform_window_wake(platform_window* w) {
  if (w && w->hwnd) PostMessageW(w->hwnd, WM_MOYU_WAKE, 0, 0);
}

// ---------- Events ----------
bool platform_poll_event(platform_window* w,
                         platform_event* out,
                         int timeout_ms) {
  if (!w || !out) return false;
  memset(out, 0, sizeof(*out));
  out->ts_ms = platform_now_ms();

  // First, drain any already-queued messages.
  MSG msg;
  if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    // Child controls such as the quick-chat EDIT need translated WM_CHAR
    // messages. Pet input still consumes the original mouse messages below.
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
    switch (msg.message) {
      case WM_QUIT: out->type = PE_QUIT; return true;
      case WM_MOUSEMOVE: {
        POINT pt = {GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam)};
        out->type = PE_MOUSE_MOVE;
        out->x = pt.x;
        out->y = pt.y;
        return true;
      }
      case WM_LBUTTONDOWN:
        SetCapture(w->hwnd);
        out->type = PE_MOUSE_DOWN;
        out->button = 0;
        out->x = GET_X_LPARAM(msg.lParam);
        out->y = GET_Y_LPARAM(msg.lParam);
        return true;
      case WM_LBUTTONDBLCLK:
        out->type = PE_MOUSE_DOUBLE_CLICK;
        out->button = 0;
        out->x = GET_X_LPARAM(msg.lParam);
        out->y = GET_Y_LPARAM(msg.lParam);
        return true;
      case WM_RBUTTONDOWN:
        SetCapture(w->hwnd);
        out->type = PE_MOUSE_DOWN;
        out->button = 1;
        out->x = GET_X_LPARAM(msg.lParam);
        out->y = GET_Y_LPARAM(msg.lParam);
        return true;
      case WM_LBUTTONUP:
        ReleaseCapture();
        out->type = PE_MOUSE_UP;
        out->button = 0;
        out->x = GET_X_LPARAM(msg.lParam);
        out->y = GET_Y_LPARAM(msg.lParam);
        return true;
      case WM_RBUTTONUP:
        ReleaseCapture();
        out->type = PE_MOUSE_UP;
        out->button = 1;
        out->x = GET_X_LPARAM(msg.lParam);
        out->y = GET_Y_LPARAM(msg.lParam);
        return true;
      case WM_DROPFILES: {
        HDROP drop = (HDROP)msg.wParam;
        wchar_t path[4096];
        UINT n = DragQueryFileW(drop, 0, path, 4096);
        if (n > 0) {
          WideCharToMultiByte(
              CP_UTF8, 0, path, -1, out->path, (int)sizeof(out->path), NULL, NULL);
          out->type = PE_DROP_FILE;
          DragFinish(drop);
          return true;
        }
        DragFinish(drop);
        return false;
      }
      case WM_MOYU_WAKE:
        out->type = PE_WAKE;
        return true;
      default:
        // Unknown message dispatched; treat as no-op.
        return false;
    }
  }

  if (timeout_ms == 0) return false;
  // Wait for a message or timeout.
  DWORD res =
      MsgWaitForMultipleObjectsEx(0,
                                  NULL,
                                  timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms,
                                  QS_ALLINPUT,
                                  MWMO_INPUTAVAILABLE);
  (void)res;
  return false;
}

// ---------- HTTP via WinHTTP ----------
static _Thread_local const char* g_http_extra_headers = NULL;
static _Thread_local const char* g_http_content_type = NULL;
static void split_url(const char* url,
                      wchar_t* scheme,
                      int scheme_cch,
                      wchar_t* host,
                      int host_cch,
                      wchar_t* path,
                      int path_cch,
                      INTERNET_PORT* port,
                      bool* is_https) {
  // url like https://host[:port]/path
  *is_https = true;
  *port = INTERNET_DEFAULT_HTTPS_PORT;
  const char* p = url;
  if (strncmp(p, "https://", 8) == 0) {
    *is_https = true;
    *port = INTERNET_DEFAULT_HTTPS_PORT;
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    *is_https = false;
    *port = INTERNET_DEFAULT_HTTP_PORT;
    p += 7;
  }
  const char* slash = strchr(p, '/');
  const char* colon = strchr(p, ':');
  const char* host_end = slash ? slash : (p + strlen(p));
  if (colon && colon < host_end) {
    MultiByteToWideChar(CP_UTF8, 0, p, (int)(colon - p), host, host_cch);
    host[colon - p] = 0;
    *port = (INTERNET_PORT)atoi(colon + 1);
  } else {
    MultiByteToWideChar(CP_UTF8, 0, p, (int)(host_end - p), host, host_cch);
    host[host_end - p] = 0;
  }
  if (slash) {
    MultiByteToWideChar(CP_UTF8, 0, slash, -1, path, path_cch);
  } else {
    path[0] = L'/';
    path[1] = 0;
  }
  if (scheme && scheme_cch > 0) scheme[0] = 0;
}

platform_http_resp platform_http_post_json(const char* url,
                                           const char* auth_bearer,
                                           const char* json_body,
                                           int timeout_ms) {
  platform_http_resp r = {0};
  if (!url || !json_body) {
    r.err = moyu_strdup("null url or body");
    return r;
  }
  LOGI("HTTP POST %s (body %zu bytes)", url, strlen(json_body));
  wchar_t host[256], path[1024];
  INTERNET_PORT port;
  bool is_https;
  split_url(url, NULL, 0, host, 256, path, 1024, &port, &is_https);

  HINTERNET hsession = WinHttpOpen(L"moyu/0.1",
                                   WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS,
                                   0);
  if (!hsession) {
    char buf[256];
    snprintf(buf, sizeof(buf), "WinHttpOpen err=%lu", GetLastError());
    LOGE("%s", buf);
    r.err = moyu_strdup(buf);
    return r;
  }
  WinHttpSetTimeouts(
      hsession, 5000, 10000, 30000, timeout_ms > 0 ? timeout_ms : 30000);

  HINTERNET hconn = WinHttpConnect(hsession, host, port, 0);
  if (!hconn) {
    char buf[256];
    snprintf(buf, sizeof(buf), "WinHttpConnect err=%lu", GetLastError());
    LOGE("%s", buf);
    r.err = moyu_strdup(buf);
    WinHttpCloseHandle(hsession);
    return r;
  }

  DWORD flags = WINHTTP_FLAG_SECURE;
  if (!is_https) flags = 0;
  HINTERNET hreq = WinHttpOpenRequest(hconn,
                                      L"POST",
                                      path,
                                      NULL,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      flags);
  if (!hreq) {
    char buf[256];
    snprintf(buf, sizeof(buf), "WinHttpOpenRequest err=%lu", GetLastError());
    LOGE("%s", buf);
    r.err = moyu_strdup(buf);
    WinHttpCloseHandle(hconn);
    WinHttpCloseHandle(hsession);
    return r;
  }

  wchar_t auth_hdr[512] = L"Authorization: Bearer ";
  {
    int n = (int)wcslen(auth_hdr);
    MultiByteToWideChar(CP_UTF8,
                        0,
                        auth_bearer ? auth_bearer : "",
                        -1,
                        auth_hdr + n,
                        (int)(512 - n));
  }
  WinHttpAddRequestHeaders(
      hreq,
      auth_hdr,
      (DWORD)-1,
      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  wchar_t ctype_buf[256] = L"Content-Type: ";
  MultiByteToWideChar(CP_UTF8,
                      0,
                      g_http_content_type ? g_http_content_type
                                          : "application/json; charset=utf-8",
                      -1,
                      ctype_buf + wcslen(ctype_buf),
                      (int)(256 - wcslen(ctype_buf)));
  WinHttpAddRequestHeaders(
      hreq,
      ctype_buf,
      (DWORD)-1,
      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
  if (g_http_extra_headers && g_http_extra_headers[0]) {
    int n = MultiByteToWideChar(
        CP_UTF8, 0, g_http_extra_headers, -1, NULL, 0);
    wchar_t* headers = (wchar_t*)moyu_alloc((size_t)n * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, g_http_extra_headers, -1, headers, n);
    WinHttpAddRequestHeaders(hreq,
                             headers,
                             (DWORD)-1,
                             WINHTTP_ADDREQ_FLAG_ADD |
                                 WINHTTP_ADDREQ_FLAG_REPLACE);
    moyu_free(headers);
  }

  size_t body_len = strlen(json_body);
  BOOL ok = WinHttpSendRequest(hreq,
                               WINHTTP_NO_ADDITIONAL_HEADERS,
                               0,
                               (LPVOID)json_body,
                               (DWORD)body_len,
                               (DWORD)body_len,
                               0);
  if (!ok) {
    char buf[256];
    snprintf(buf, sizeof(buf), "WinHttpSendRequest err=%lu", GetLastError());
    LOGE("%s", buf);
    r.err = moyu_strdup(buf);
    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(hconn);
    WinHttpCloseHandle(hsession);
    return r;
  }
  if (!WinHttpReceiveResponse(hreq, NULL)) {
    char buf[256];
    snprintf(
        buf, sizeof(buf), "WinHttpReceiveResponse err=%lu", GetLastError());
    LOGE("%s", buf);
    r.err = moyu_strdup(buf);
    WinHttpCloseHandle(hreq);
    WinHttpCloseHandle(hconn);
    WinHttpCloseHandle(hsession);
    return r;
  }

  // Status code
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(hreq,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX,
                      &status,
                      &size,
                      WINHTTP_NO_HEADER_INDEX);
  r.status = (int)status;
  {
    wchar_t value[512];
    DWORD bytes = sizeof(value);
    if (WinHttpQueryHeaders(hreq,
                            WINHTTP_QUERY_CONTENT_TYPE,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            value,
                            &bytes,
                            WINHTTP_NO_HEADER_INDEX)) {
      int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
      r.content_type = (char*)moyu_alloc((size_t)n);
      WideCharToMultiByte(
          CP_UTF8, 0, value, -1, r.content_type, n, NULL, NULL);
    }
    bytes = sizeof(value);
    if (WinHttpQueryHeaders(hreq,
                            WINHTTP_QUERY_CUSTOM,
                            L"MCP-Session-Id",
                            value,
                            &bytes,
                            WINHTTP_NO_HEADER_INDEX)) {
      int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
      r.session_id = (char*)moyu_alloc((size_t)n);
      WideCharToMultiByte(
          CP_UTF8, 0, value, -1, r.session_id, n, NULL, NULL);
    }
  }

  // Body
  size_t total = 0, cap = 8192;
  char* body = (char*)moyu_alloc(cap);
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(hreq, &avail) && avail > 0) {
    if (total + avail + 1 > cap) {
      while (total + avail + 1 > cap)
        cap *= 2;
      body = (char*)moyu_realloc(body, cap);
    }
    DWORD rd = 0;
    if (!WinHttpReadData(hreq, body + total, avail, &rd) || rd == 0) break;
    total += rd;
  }
  body[total] = 0;
  r.body = body;
  r.body_len = total;

  WinHttpCloseHandle(hreq);
  WinHttpCloseHandle(hconn);
  WinHttpCloseHandle(hsession);
  return r;
}

platform_http_resp platform_http_request(const char* method,
                                         const char* url,
                                         const char* auth_bearer,
                                         const char* extra_headers,
                                         const char* body,
                                         const char* content_type,
                                         int timeout_ms) {
  if (method && strcmp(method, "POST") != 0) {
    platform_http_resp r = {0};
    r.err = moyu_strdup("only POST is implemented by the Windows HTTP adapter");
    return r;
  }
  g_http_extra_headers = extra_headers;
  g_http_content_type = content_type;
  platform_http_resp r =
      platform_http_post_json(url, auth_bearer, body ? body : "", timeout_ms);
  g_http_extra_headers = NULL;
  g_http_content_type = NULL;
  return r;
}

void platform_http_resp_free(platform_http_resp* r) {
  if (!r) return;
  if (r->body) {
    moyu_free(r->body);
    r->body = NULL;
  }
  if (r->err) {
    moyu_free(r->err);
    r->err = NULL;
  }
  if (r->content_type) {
    moyu_free(r->content_type);
    r->content_type = NULL;
  }
  if (r->session_id) {
    moyu_free(r->session_id);
    r->session_id = NULL;
  }
  r->body_len = 0;
  r->status = 0;
}

// ---------- CJK glyph rasterization ----------
// Cached font handle for 12px; cheap to keep around.
static HFONT g_cjk_font = NULL;
static int g_cjk_font_size = 0;

static HFONT get_cjk_font(int pixel_size) {
  if (g_cjk_font && g_cjk_font_size == pixel_size) return g_cjk_font;
  if (g_cjk_font) DeleteObject(g_cjk_font);
  LOGFONTW lf = {0};
  lf.lfHeight = -pixel_size;
  lf.lfWeight = FW_NORMAL;
  lf.lfCharSet = DEFAULT_CHARSET;
  lf.lfQuality = NONANTIALIASED_QUALITY;
  lf.lfOutPrecision = OUT_TT_PRECIS;
  lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  wcscpy(lf.lfFaceName, L"Microsoft YaHei");
  g_cjk_font = CreateFontIndirectW(&lf);
  g_cjk_font_size = pixel_size;
  return g_cjk_font;
}

uint8_t* platform_get_glyph(uint32_t codepoint,
                            int pixel_size,
                            int* w,
                            int* h) {
  if (!w || !h) return NULL;
  *w = 0;
  *h = 0;
  const int canvas = pixel_size * 3;
  BITMAPINFO bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = canvas;
  bi.bmiHeader.biHeight = -canvas;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;

  void* bits = NULL;
  HDC screen = GetDC(NULL);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, screen);
  if (!dc || !bmp || !bits) {
    if (dc) DeleteDC(dc);
    if (bmp) DeleteObject(bmp);
    return NULL;
  }

  HFONT font = get_cjk_font(pixel_size);
  HGDIOBJ old_bmp = SelectObject(dc, bmp);
  HGDIOBJ old_font = SelectObject(dc, font);
  RECT rc = {0, 0, canvas, canvas};
  HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
  FillRect(dc, &rc, bg);
  DeleteObject(bg);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));

  wchar_t wc[2] = {(wchar_t)codepoint, 0};
  TextOutW(dc, pixel_size / 2, pixel_size / 3, wc, 1);

  uint32_t* px = (uint32_t*)bits;
  int min_x = canvas, min_y = canvas, max_x = -1, max_y = -1;
  for (int y = 0; y < canvas; y++) {
    for (int x = 0; x < canvas; x++) {
      uint32_t p = px[y * canvas + x];
      if ((p & 0x00FFFFFFu) != 0) {
        if (x < min_x) min_x = x;
        if (y < min_y) min_y = y;
        if (x > max_x) max_x = x;
        if (y > max_y) max_y = y;
      }
    }
  }

  if (max_x < min_x || max_y < min_y) {
    SelectObject(dc, old_font);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return NULL;
  }

  *w = max_x - min_x + 1;
  *h = max_y - min_y + 1;
  int row_bytes = ((*w + 31) / 32) * 4;
  uint8_t* out = (uint8_t*)moyu_alloc((size_t)row_bytes * (size_t)(*h));
  memset(out, 0, (size_t)row_bytes * (size_t)(*h));
  for (int y = 0; y < *h; y++) {
    for (int x = 0; x < *w; x++) {
      uint32_t p = px[(min_y + y) * canvas + (min_x + x)];
      if ((p & 0x00FFFFFFu) != 0) {
        out[y * row_bytes + (x / 8)] |= (uint8_t)(0x80 >> (x % 8));
      }
    }
  }

  SelectObject(dc, old_font);
  SelectObject(dc, old_bmp);
  DeleteObject(bmp);
  DeleteDC(dc);
  return out;
}

bool platform_render_text(const char* utf8,
                          int pixel_height,
                          int max_width,
                          uint32_t rgba,
                          platform_text_bitmap* out) {
  if (!out) return false;
  ZeroMemory(out, sizeof(*out));
  if (!utf8 || !utf8[0] || pixel_height <= 0 || max_width <= 0) return false;

  int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     utf8, -1, NULL, 0);
  if (wide_len <= 0) return false;
  wchar_t* wide = (wchar_t*)moyu_alloc((size_t)wide_len * sizeof(wchar_t));
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, wide_len);

  HDC screen = GetDC(NULL);
  HDC measure_dc = CreateCompatibleDC(screen);
  HFONT font = CreateFontW(-pixel_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  HGDIOBJ old_font = SelectObject(measure_dc, font);
  RECT measured = {0, 0, max_width, 0};
  UINT flags = DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;
  DrawTextW(measure_dc, wide, -1, &measured, flags);
  int width = measured.right - measured.left;
  int height = measured.bottom - measured.top;
  if (width < 1) width = 1;
  if (height < pixel_height) height = pixel_height;
  if (width > max_width) width = max_width;

  BITMAPINFO bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = width;
  bi.bmiHeader.biHeight = -height;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* dib_bits = NULL;
  HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS,
                                 &dib_bits, NULL, 0);
  HDC draw_dc = CreateCompatibleDC(screen);
  HGDIOBJ old_dib = dib ? SelectObject(draw_dc, dib) : NULL;
  HGDIOBJ draw_old_font = draw_dc ? SelectObject(draw_dc, font) : NULL;
  if (!dib || !draw_dc || !dib_bits) {
    if (draw_dc) DeleteDC(draw_dc);
    if (dib) DeleteObject(dib);
    SelectObject(measure_dc, old_font);
    DeleteObject(font);
    DeleteDC(measure_dc);
    ReleaseDC(NULL, screen);
    moyu_free(wide);
    return false;
  }

  memset(dib_bits, 0, (size_t)width * (size_t)height * 4);
  SetBkMode(draw_dc, TRANSPARENT);
  SetTextColor(draw_dc, RGB(255, 255, 255));
  RECT target = {0, 0, width, height};
  DrawTextW(draw_dc, wide, -1, &target,
            DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);

  uint8_t rr = (uint8_t)((rgba >> 24) & 0xff);
  uint8_t gg = (uint8_t)((rgba >> 16) & 0xff);
  uint8_t bb = (uint8_t)((rgba >> 8) & 0xff);
  uint8_t base_a = (uint8_t)(rgba & 0xff);
  uint32_t* src = (uint32_t*)dib_bits;
  out->pixels = (uint32_t*)moyu_alloc((size_t)width * (size_t)height * 4);
  out->w = width;
  out->h = height;
  for (int i = 0; i < width * height; i++) {
    uint8_t coverage = (uint8_t)(src[i] & 0xff);
    uint8_t alpha = (uint8_t)((coverage * base_a) / 255);
    out->pixels[i] = ((uint32_t)rr << 24) | ((uint32_t)gg << 16) |
                     ((uint32_t)bb << 8) | alpha;
  }

  SelectObject(draw_dc, draw_old_font);
  SelectObject(draw_dc, old_dib);
  DeleteDC(draw_dc);
  DeleteObject(dib);
  SelectObject(measure_dc, old_font);
  DeleteObject(font);
  DeleteDC(measure_dc);
  ReleaseDC(NULL, screen);
  moyu_free(wide);
  return true;
}

void platform_text_bitmap_free(platform_text_bitmap* bitmap) {
  if (!bitmap) return;
  if (bitmap->pixels) moyu_free(bitmap->pixels);
  ZeroMemory(bitmap, sizeof(*bitmap));
}
