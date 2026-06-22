# MOYU

MOYU is a lightweight autonomous desktop companion and a small C/Lua agent runtime.

It is not a random animation widget and not a generic office assistant. Within user-approved boundaries, it forms beliefs, habits, and a small active intention, observes the world through built-in tools or MCP, stores durable memories in human-readable files, and lets the human correct, shape, and prune that memory over time.

Chinese README: [README.zh-CN.md](README.zh-CN.md)

## Core capabilities

- Persistent identity: `~/.moyu/SOUL.md`, `MEMORY.md`, and SQLite state survive restarts.
- Bounded autonomy: one intention at a time, up to three steps, persistent budget, permissions, and deadlines.
- Human shaping: allow, deny, correct, remember, and forget all change later behavior.
- Real MCP: stdio and Streamable HTTP tool discovery are supported.
- Terminal chat: double-click the pet or open `moyu-chat.exe` to talk to the same runtime without a WebView or Electron.
- Desktop interaction: tray icon, native Windows context menu, drag-and-drop intake, hover feedback, and draggable repositioning.
- Multimodal intake: text files are previewed locally and summarized by the main model; images can be analyzed by a separate vision model.
- Low resource usage: event-driven rendering and blocking model work on a background worker.

## Build

Windows requires Visual Studio 2022 C++ Build Tools and `clang-cl`:

```bat
build.bat
```

Outputs:

```text
build/moyu.exe
build/moyu-chat.exe
build/moyu-tests.exe
build/moyu-mcp-git.exe
build/moyu-mcp-notes.exe
build/moyu-mcp-weather.exe
```

Run tests:

```bat
build\moyu-tests.exe
```

Smoke-test the current LLM configuration:

```bat
build\moyu.exe --smoke-llm
```

## Run

```bat
build\moyu.exe
```

On first launch MOYU creates `~/.moyu/`, default SOUL and MEMORY files, a SQLite state database, and onboarding prompts for an observation root and low-frequency autonomy.

Interaction:

- Double-click the pet to open terminal chat.
- Right-click the pet to open a native Windows context menu.
- Right-click the tray icon to open the same native Windows context menu.
- Drag a file or folder onto the pet to feed it something to inspect or keep.
- Drag the pet to move it to another place on the desktop.

## Configuration

Long-lived configuration lives in `~/.moyu/config.json`.

- Runtime config is loaded from disk at startup. It is not compiled into the binaries.
- API keys are stored in `~/.moyu/secrets.dat` through Windows DPAPI, not plaintext config.
- The config can include `llm`, `vision`, `owner`, `appearance`, `privacy`, `autonomy`, and `mcp_servers`.

See the complete example in [assets/config.example.json](assets/config.example.json).

Example stdio MCP server:

```json
{
  "name": "git-local",
  "transport": "stdio",
  "command": "D:\\path\\moyu-mcp-git.exe",
  "args": []
}
```

Example Streamable HTTP MCP server:

```json
{
  "name": "weather-local",
  "transport": "streamable_http",
  "url": "http://127.0.0.1:7788/mcp"
}
```

## Notes

- Windows is the fully supported platform today. Linux and macOS backends are still compile stubs.
- `observe` permissions can be durable, `draft` writes only private output, and `mutate` requires an explicit human action.
- User files are not copied wholesale into SQLite. Large tool results are not retained indefinitely.

## Documentation

- [Architecture](docs/architecture.md)
- [Design](docs/design.md)
- [Context and Memory](docs/context.md)
- [MCP and Tools](docs/mcp.md)
- [Lua Policy API](docs/lua_api.md)
- [Build and Test](docs/build.md)
- [User Guide](docs/user-guide.md)
- [Chinese User Guide](docs/user-guide.zh-CN.md)

## License

MIT. Lua and cJSON are MIT; the SQLite amalgamation is public domain. See `third_party/`.
