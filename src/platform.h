#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// platform.h — the ONLY abstraction in MOYU.
// Each platform (Win32/Linux/macOS) provides its own .c/.m implementation.
// Everything else in the codebase calls these directly; no further abstraction.

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Window ----------
typedef struct platform_window platform_window;

platform_window* platform_window_create(int w,
                                        int h,
                                        int x,
                                        int y,
                                        bool transparent,
                                        bool topmost,
                                        bool click_through);
void platform_window_destroy(platform_window* w);
// Blit a RGBA8888 buffer (w*h pixels) into the window. Pixel layout: 0xRRGGBBAA.
void platform_window_set_pixels(platform_window* w,
                                const uint32_t* rgba,
                                int w_,
                                int h_);
void platform_window_move(platform_window* w, int x, int y);
void platform_window_get_pos(platform_window* w, int* x, int* y);
void platform_window_show(platform_window* w);
void platform_window_hide(platform_window* w);

// Define the sub-rectangle of the window that should capture mouse input.
// Pixels outside this rect always pass through (HTTRANSPARENT). Inside the
// rect, alpha-based hit-testing still applies. Default = entire window.
void platform_window_set_clickable(
    platform_window* w, int x, int y, int w_, int h_);

// Get mouse position in screen coordinates (regardless of which window it's over).
void platform_get_cursor_pos(int* x, int* y);

// ---------- Events ----------
typedef enum {
  PE_NONE = 0,
  PE_MOUSE_MOVE,  // mouse moved over the pet
  PE_MOUSE_HOVER_ENTER,
  PE_MOUSE_HOVER_LEAVE,
  PE_MOUSE_DOWN,
  PE_MOUSE_DOUBLE_CLICK,
  PE_MOUSE_UP,
  PE_DROP_FILE,
  PE_TIMER,  // periodic tick
  PE_WAKE,   // background worker completion
  PE_QUIT,
} platform_event_type;

typedef struct {
  platform_event_type type;
  int x, y;    // window-relative coords for mouse events
  int button;  // 0=left,1=right,2=middle
  char path[4096];
  uint64_t ts_ms;
} platform_event;

// Poll one event. timeout_ms: -1 = block, 0 = non-blocking, >0 = wait up to N ms.
bool platform_poll_event(platform_window* w,
                         platform_event* out,
                         int timeout_ms);
void platform_window_wake(platform_window* w);

// ---------- Time ----------
uint64_t platform_now_ms(void);
// Wall clock milliseconds since Unix epoch. Use for persisted timestamps only.
uint64_t platform_unix_ms(void);
void platform_sleep_ms(uint32_t ms);

// ---------- HTTP (platform-native HTTPS) ----------
typedef struct {
  int status;  // HTTP status code (0 if transport failed)
  char* body;  // null-terminated; caller frees with platform_http_resp_free
  size_t body_len;
  char* err;  // null-terminated error string, or NULL
  char* content_type;
  char* session_id;
} platform_http_resp;

platform_http_resp platform_http_request(const char* method,
                                         const char* url,
                                         const char* auth_bearer,
                                         const char* extra_headers,
                                         const char* body,
                                         const char* content_type,
                                         int timeout_ms);

// POST JSON with Bearer auth. url must include scheme (https://...).
// Returns zeroed resp on transport failure with err set.
platform_http_resp platform_http_post_json(const char* url,
                                           const char* auth_bearer,
                                           const char* json_body,
                                           int timeout_ms);
void platform_http_resp_free(platform_http_resp* r);

// ---------- Filesystem ----------
// Directory of the running executable (for finding assets/scripts).
const char* platform_exe_dir(void);
char* platform_read_file(const char* path,
                         size_t* out_len);  // returns NULL on failure
bool platform_write_file(const char* path, const void* data, size_t len);
bool platform_write_file_atomic(const char* path, const void* data, size_t len);
bool platform_file_exists(const char* path);
bool platform_make_dirs(const char* path);
bool platform_remove_file(const char* path);
bool platform_move_file(const char* from, const char* to, bool replace);

// User profile directory in UTF-8. Returned pointer is process-lifetime storage.
const char* platform_home_dir(void);

// Build a path "<dir>/<name>". Returned string is heap-allocated.
char* platform_join_path(const char* dir, const char* name);

// ---------- CJK glyph rasterization ----------
// Render a single Unicode codepoint as a 1bpp bitmap (MSB-first, each row
// padded to 4-byte boundary — same layout as Win32 GGO_BITMAP).
// Returns NULL on failure. On success, *w/*h get the black-box dimensions
// and the caller owns the returned buffer (moyu_free).
// pixel_size is the desired font height in pixels (e.g. 12).
uint8_t* platform_get_glyph(uint32_t codepoint, int pixel_size, int* w, int* h);

#ifdef __cplusplus
}
#endif
