# Architecture

MOYU is a single-threaded desktop pet agent runtime in C11 with an embedded
Lua sandbox. The design obeys one rule: **only platform code is abstracted;
everything else is direct.** This keeps the hot path short and the call
stacks shallow.

## Module map

All sources live in a flat `src/` directory. There is no nested
subdirectory layout by design — see [design.md](design.md) for the
rationale.

```
src/
├── main.c              Assembly + WinMain entry
├── loop.{h,c}          Event-driven main loop, owns moyu_app state
├── context.{h,c}       Ring buffer of context_node — the agent's memory
├── event.{h,c}         Internal event queue (anim, say, llm_trigger, ...)
├── persona.{h,c}       Personality (4 floats, persisted)
├── emotion.{h,c}       Valence/arousal with personality-biased random walk
├── llm.{h,c}           OpenAI-compatible chat completion + LRU cache
├── tool.{h,c}          Tool registry: name → C function
├── mcp.{h,c}           MCP client (HTTP transport, dynamic tool loading)
├── render.{h,c}        RGBA8888 backbuffer + blit + alpha
├── sprite.{h,c}        Sprite sheet struct + frame stepping
├── procedural.{h,c}    Code-generated 32×32 sprites (idle/blink/sleep/...)
├── font.{h,c}          5×7 ASCII + CJK glyph rendering via platform layer
├── lua_rt.{h,c}        Lua sandbox + moyu.* API + hooks
├── platform.h          The ONLY abstraction — see below
├── platform_win32.c    Full Win32 implementation
├── platform_linux.c    X11 stub (compiles, runtime-not-implemented)
├── platform_macos.m    Cocoa stub (compiles, runtime-not-implemented)
├── log.{h,c}           Level-tagged logging to stderr + file
├── mem.{h,c}           malloc/realloc/free wrappers (statistics hook)
└── hash.{h,c}          FNV-1a 64-bit
```

## The platform boundary

`platform.h` defines the only interface that varies by OS. Each platform
provides one implementation file. The rest of the codebase calls these
functions directly — there is no "platform abstraction layer" object, no
vtable, no factory.

| Concern | Interface |
|---|---|
| Window | `platform_window_create / set_pixels / set_clickable / move / show / hide` |
| Events | `platform_poll_event` (single call, timeout-aware) |
| Time | `platform_now_ms`, `platform_sleep_ms` |
| HTTP | `platform_http_post_json` (sync, returns status + body + err) |
| Files | `platform_exe_dir`, `platform_read_file`, `platform_write_file`, `platform_join_path` |
| Glyphs | `platform_get_glyph(codepoint, pixel_size, &w, &h)` for CJK rendering |

Adding a new platform = adding one `.c` file that implements these. Nothing
else in the tree changes.

## Runtime data flow

```
                 ┌──────────────┐
   Win32 msgs──▶ │ platform_    │──▶ platform_event ──┐
                 │ poll_event   │                     │
                 └──────────────┘                     ▼
                                            ┌────────────────┐
                                            │   loop.c       │
   Lua hooks (on_tick/on_click) ◀──────────│   moyu_app_step │
         │                                  └────────────────┘
         ▼                                          │
   ┌──────────────┐                                 ▼
   │ moyu.* API   │                          internal events
   │  emit/say/   │                                 │
   │  anim/llm    │                                 ▼
   └──────────────┘                         ┌────────────────┐
         │                                  │  event_queue   │──▶ anim switch
         │ sync (blocks Lua)                └────────────────┘──▶ say bubble
         ▼                                          │
   ┌──────────────┐                                 ▼
   │ llm_complete │ ◀──── llm_cache (LRU 64) ──  render frame
   │  (HTTP POST) │                                 │
   └──────────────┘                                 ▼
         │                                  platform_window_set_pixels
         ▼
   context_push(CTX_REFLECTION, prompt, response)
```

Key properties:
- **Single thread.** LLM calls are synchronous from the loop's perspective.
  The UI freezes briefly during an LLM round-trip; this is intentional —
  the pet is allowed to "think" visibly.
- **1 Hz tick.** `platform_poll_event` is called with a 1000 ms timeout,
  which doubles as the idle wake-up. Idle CPU is ~0.
- **Throttling.** A ring of 128 timestamps tracks LLM calls in a sliding
  24-hour window; `llm_daily_limit` (default 50) caps total calls.

## State ownership

All runtime state lives in a single `moyu_app` struct (`loop.h`). No
globals, no singletons. `main.c` allocates it on the stack, passes
pointers down, and tears everything down in reverse order at exit.

## Build outputs

One executable, ~600 KB. Lua 5.4 and cJSON are linked statically. No
runtime DLLs beyond the OS-provided ones (`winhttp`, `user32`, `gdi32`,
`shell32`, `ws2_32` on Windows).
