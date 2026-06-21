# Build and Verification

## Windows

Requirements: Visual Studio 2022 C++ Build Tools and clang-cl.

```bat
build.bat
```

The script performs a clean-order build of the desktop runtime, terminal chat, three reference MCP servers and test runner. It statically compiles Lua, cJSON and SQLite.

Outputs:

```text
build/moyu.exe
build/moyu-chat.exe
build/moyu-tests.exe
build/moyu-mcp-git.exe
build/moyu-mcp-notes.exe
build/moyu-mcp-weather.exe
```

Expected main binary size is currently about 1.8 MB.

## Tests

```bat
build\moyu-tests.exe
```

Coverage includes workdir creation, atomic Markdown memory, secret filtering, beliefs, intentions, permissions, budgets, corrupt database recovery, stdio MCP and Streamable HTTP MCP/session discovery.

Terminal command smoke test:

```powershell
@('/status', '/collections', '/drafts', '/tools', '/exit') |
  .\build\moyu-chat.exe
```

## LLM smoke

With the local ignored `assets/config.json` or migrated DPAPI secret:

```bat
build\moyu.exe --smoke-llm
```

Exit code 0 requires the configured model to return `MOYU_SMOKE_OK`. The API key and response body are not logged.

## CMake

```powershell
cmake -S . -B build-cmake
cmake --build build-cmake --config Release
```

If `cmake` is not on PATH, use the copy bundled with Visual Studio Build Tools.

## Runtime layout

Assets and scripts are copied next to the binary. Mutable user state never belongs in the build tree; it lives under `~/.moyu`.

## Other platforms

`build.sh` compiles SQLite and the common runtime. Linux/macOS desktop/window and HTTP backends remain stubs in v0.2.
