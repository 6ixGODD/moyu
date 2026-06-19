# MOYU — a pixel desktop pet agent runtime in C + Lua

<p align="center">
  <img alt="size" src="https://img.shields.io/badge/binary-~600KB-blue">
  <img alt="lang" src="https://img.shields.io/badge/C11-flat%20src-yellow">
  <img alt="lua" src="https://img.shields.io/badge/Lua-5.4%20sandbox-green">
  <img alt="deps" src="https://img.shields.io/badge/deps-0%20external-success">
  <img alt="platform" src="https://img.shields.io/badge/platform-Windows%20%E2%9C%93%20%7C%20Linux%20macOS%20stub-lightgrey">
</p>

> 摸鱼 (mō yú) — verb. To slack off at work. To idle while pretending to be busy. A national pastime.

MOYU is a tiny pixel-art desktop companion that lives in your screen's
bottom-right corner and occasionally judges you for being on the
computer again. It is also a deliberately minimal **agent runtime** — a
sandboxed Lua hook system riding on top of a C core that renders one
transparent window, polls events at 1 Hz, and talks to an OpenAI-compatible
LLM endpoint only when something interesting happens.

**Design constraints (taken literally):**

| Constraint | How |
|---|---|
| <10 MB binary | ~600 KB. Lua 5.4 + cJSON linked statically. Sprites generated in code. |
| ≤1 Hz tick, idle CPU ≈ 0 | Single thread, blocking poll with 1000 ms timeout. No busy loops. |
| 90% rule-based, 10% LLM | All behaviour branches in `scripts/default.lua`. LLM is a reflective narrator, not an autonomous agent. |
| Pixel-level rendering | Software RGBA8888 rasterizer → `UpdateLayeredWindow`. No GPU, no WebView, no SVG. |
| UI works without network | LLM disabled gracefully on empty `api_key`. Falls back to pure rule behaviour. |
| Only platform code is abstracted | One file, `platform.h`. Everything else is free functions on plain structs. |

---

## What it looks like

A 96×96 pixel creature sits in a 160×160 transparent, topmost, click-through
window. The transparent area passes mouse events to the desktop; only the
pet's body captures clicks.

| Animation | Trigger |
|---|---|
| `idle` | default — gentle breathing |
| `blink` | random short blink |
| `observe` | cursor within 60 px |
| `happy` | click, or positive valence |
| `sad` | negative valence |
| `sleep` | 2+ minutes idle |

Speech bubbles support full CJK (rasterized via `GetGlyphOutlineW`) — your
pet can judge you in any language.

---

## Build

### Windows (clang-cl + MSVC linker)

```bat
build.bat
```

Output: `build/moyu.exe` (~600 KB). Assets and scripts are copied next to
the binary automatically.

Requirements: Visual Studio 2022 BuildTools or Community (for `vcvarsall.bat`
+ MSVC linker) and `clang-cl` on PATH.

### Cross-platform (clang, no MSVC)

```bash
./build.sh
```

Detects host. On Windows produces the same `.exe`; on Linux/macOS produces
a stub binary that compiles cleanly but prints "not implemented" at runtime.

### CMake

```bash
cmake -B build -S .
cmake --build build --config Release
```

---

## Configure

Edit `assets/config.json`:

```json
{
  "llm": {
    "base_url": "https://api.deepseek.com/v1",
    "api_key": "sk-...",
    "model": "deepseek-v4-flash",
    "max_tokens": 256,
    "temperature": 0.8,
    "daily_limit": 50
  },
  "personality": { "mood_bias": 0.0, "sarcasm": 0.3, "curiosity": 0.6, "patience": 0.7 },
  "rules": { "idle_poke_seconds": 300, "rare_event_chance": 0.02 },
  "mcp_servers": []
}
```

Leave `api_key` empty → MOYU runs rule-only, no network calls. The LLM is
gated behind a daily-limit ring (default 50 calls / 24 h) and an LRU cache
keyed on the rolling context hash, so identical recent histories don't
re-hit the API.

---

## Run

```bat
build\moyu.exe
```

The pet appears in the bottom-right of your primary screen. Logs go to
`moyu.log` next to the executable (and to `stderr` if launched from a
console).

---

## Make it yours

Behaviour lives in `scripts/default.lua`. Replace the file — that is the
entire customisation story. The Lua sandbox strips `io`/`os`/`package`/
`loadfile`/`require`/`load` and exposes a ~10-function API:

```lua
function on_tick(idle_s, mouse_dist, valence, arousal)
  if mouse_dist < 60 then
    moyu.anim("observe")
  elseif idle_s > 120 then
    moyu.anim("sleep")
  end

  if idle_s > 300 and math.random() < 0.05 then
    moyu.say("又摸鱼？", 3000)
  end

  -- Reflective LLM: rare, gated by curiosity, throttled by runtime
  if idle_s > 600 and math.random() < 0.002 then
    local _, _, curiosity, _ = moyu.personality()
    if curiosity > 0.4 then
      local reply = moyu.llm("One short idle thought.")
      if reply then moyu.say(reply, 4000) end
    end
  end
end
```

Full API reference: [docs/lua_api.md](docs/lua_api.md).

---

## Architecture in one paragraph

One thread. One `moyu_app` struct on the stack of `main`. A ring buffer
of `context_node`s is the agent's only memory; each user interaction,
tool call, or LLM reflection pushes one node. On every tick, the loop
polls the platform, drains an internal event queue, calls the Lua
`on_tick` hook, and blits a procedurally-generated sprite frame. LLM
calls are synchronous (the pet visibly pauses to think) and feed back
into the context store as `CTX_REFLECTION` nodes. Everything
platform-specific — window, events, HTTP, time, filesystem, CJK glyph
rasterization — lives behind `platform.h`. Everything else is free
functions on plain structs. No frameworks, no interfaces, no
factories.

More: [docs/architecture.md](docs/architecture.md) ·
[docs/design.md](docs/design.md) ·
[docs/context.md](docs/context.md) ·
[docs/mcp.md](docs/mcp.md)

---

## Project layout

```
src/
  main.c              WinMain entry + assembly
  loop.{h,c}          1 Hz event-driven main loop
  context.{h,c}       Rolling ring buffer — the agent's memory
  event.{h,c}         Internal event queue
  persona.{h,c}       Personality (4 floats, persisted)
  emotion.{h,c}       Valence/arousal, personality-biased random walk
  llm.{h,c}           OpenAI-compatible completion + LRU cache
  tool.{h,c}          Tool registry: name → C function
  mcp.{h,c}           MCP client over HTTP (dynamic tool loading)
  render.{h,c}        RGBA8888 backbuffer + alpha blit
  sprite.{h,c}        Sprite sheet struct + frame stepping
  procedural.{h,c}    Code-generated 32×32 sprites
  font.{h,c}          5×7 ASCII + CJK via platform glyph rasterizer
  lua_rt.{h,c}        Lua 5.4 sandbox + moyu.* API + hooks
  platform.h          The ONLY abstraction
  platform_win32.c    Full Win32: layered window, WinHTTP, GGO glyph
  platform_linux.c    X11 stub (compiles)
  platform_macos.m    Cocoa stub (compiles)
  log.{h,c}  mem.{h,c}  hash.{h,c}
third_party/
  lua/                Lua 5.4.7 vendored
  cjson/              cJSON 1.7.18 vendored
assets/config.json
scripts/default.lua
docs/
```

---

## Roadmap

- [ ] Linux X11 + libcurl platform implementation
- [ ] macOS Cocoa + NSURLSession platform implementation
- [ ] MCP stdio transport (currently HTTP only)
- [ ] Persistence of personality drift and say history across restarts
- [ ] `moyu.tool(name, args)` Lua binding so scripts can call MCP tools directly
- [ ] Replace procedural sprites with optional BMP loading

---

## License

MIT. See [LICENSE](LICENSE). Lua 5.4 (MIT) and cJSON (MIT) are vendored
under `third_party/` and retain their original licenses.
