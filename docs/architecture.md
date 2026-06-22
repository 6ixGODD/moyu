# Architecture

## Runtime flow

```text
Win32 event / timer / worker completion
  -> short-term context ring
  -> belief + drive update
  -> Lua policy proposes behavior
  -> C BDI scheduler validates budget and permission
  -> one intention, at most three steps
  -> builtin or MCP tool
  -> episode / belief / habit / inbox
  -> optional MEMORY.md promotion
  -> dirty render + optional bubble / terminal inbox
```

`moyu_app` owns the process lifetime. `main.c` assembles modules; `loop.c` is the event pump; business state lives in dedicated modules rather than accumulating in the loop.

## State layers

- `context_store`: bounded in-memory working context for the current process.
- `state_store`: SQLite repository for episodes, beliefs, drives, relationships, intentions, permissions, budgets, chat and MCP metadata.
- `memory_system`: user-readable SOUL/MEMORY loading, compose, promotion, forgetting and atomic backup.
- `agent_runtime`: single active intention and event-driven BDI policy.

SQLite transactions make intention transitions and budgets durable. An expired intention recovered after a crash is failed rather than repeated. A corrupt database is moved to `.corrupt-<timestamp>` before a clean database is created.

## Process and concurrency model

`moyu.exe` owns the pet surface and autonomous runtime. Its Win32 rendering and Lua state remain on the main thread. A single condition-variable worker runs blocking LLM/HTTP work. Completion posts `WM_APP+42`, waking `MsgWaitForMultipleObjectsEx` without polling.

`moyu-chat.exe` is a separate console process. It opens the same SQLite database in WAL mode, reloads SOUL/MEMORY, decrypts the same DPAPI secret, and independently connects configured MCP servers. Closing it cannot stop the pet. The tray's native Windows menu launches it in a new console; double-clicking the pet instead opens the in-process quick prompt. Each stdio MCP server is a child process; calls are synchronous and explicitly initiated in the TUI.

## Rendering

The engine keeps RGBA8888 pixels and presents premultiplied BGRA through `UpdateLayeredWindow`. `render_dirty` prevents composition and upload when nothing changed. The behavior scheduler uses a 20 Hz cadence while walking or dodging and 10 Hz for quiet poses. It chooses from 20 bounded procedural animations and stops uploading unchanged frames.

The builtin 48×48 cream spirit is generated in `procedural.c`; BMP skins remain supported. The window is 160×160 and the 96×96 rendered pet area performs alpha hit testing.

## Platform boundary

`platform.h` remains the only OS abstraction and covers window/events, time, HTTP, filesystem, atomic writes, paths and glyphs. Windows is complete. Linux/macOS implement filesystem/time compatibility plus runtime stubs for the desktop surface.

## Dependencies

- Lua 5.4 sandbox
- cJSON
- SQLite 3.53.2 amalgamation
- Win32 system libraries: User32, GDI32, Shell32, Ole32, Crypt32, WinHTTP, Winsock

No WebView, Electron, Node, Python runtime, curses dependency, GPU framework or external Agent framework is used.
