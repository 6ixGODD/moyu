# Build

MOYU builds as a single self-contained executable. Three build paths are
provided; pick whichever matches your toolchain.

## Windows (recommended)

Requirements: Visual Studio 2022 (BuildTools or Community) for the MSVC
linker + `clang-cl` (ships with VS or as LLVM).

```bat
build.bat
```

What it does:
1. Locates `vcvarsall.bat` under VS 2022 and calls it for `x64`.
2. Compiles every `src/*.c` (excluding `platform_linux.c` and
   `platform_macos.m`) with `clang-cl /std:c11 /O2`.
3. Links `moyu.exe` against `user32 gdi32 shell32 ws2_32 winhttp`.
4. Copies `assets/` and `scripts/` next to the binary.

Output: `build/moyu.exe` (~600 KB).

### Direct clang (no MSVC)

If you have clang but want to skip `vcvarsall` (e.g. from an MSYS2 shell):

```bash
./build.sh
```

Detects the host automatically. On Windows it links the same Win32 libs.

## Linux / macOS

Both platform backends are stubs that compile but print "not implemented"
at runtime. The build still produces a binary so you can develop the
non-platform code:

```bash
./build.sh
```

- Linux links `libcurl` + `libX11` (auto-detected via `pkg-config`).
- macOS links `AppKit` + `Foundation` and compiles `platform_macos.m`.

## CMake

Alternative for IDE users (CLion, VS Code CMake Tools):

```bash
cmake -B build -S .
cmake --build build --config Release
```

The CMake build flattens the same `src/*.c` glob, excludes the irrelevant
platform files, and copies `assets/` + `scripts/` as a post-build step.

## Runtime layout

After building, the binary directory looks like:

```
moyu.exe
moyu.log              # created at runtime
assets/
  config.json         # LLM key, personality, rules
scripts/
  default.lua         # personality + behaviour rules
```

All paths resolve relative to the executable directory, so the folder is
relocatable.

## Verifying it runs

1. Edit `assets/config.json` — set `llm.api_key` to your DeepSeek key
   (or leave empty to run rule-only).
2. Launch `moyu.exe`. A 160×160 transparent window appears in the
   bottom-right of the primary screen, with a 96×96 pixel pet centered in
   the bottom of that window.
3. Hover the mouse over the pet → it switches to `observe`.
4. Wait 5+ minutes idle → occasional speech bubbles.
5. Logs go to `moyu.log` next to the exe (and to `stderr` if launched
   from a console).

## Troubleshooting

| Symptom | Fix |
|---|---|
| `LNK1104 cannot open moyu.exe` | An old instance is still running. `taskkill /f /im moyu.exe` then rebuild. |
| `vcvarsall.bat not found` | Install VS 2022 BuildTools C++ component. |
| CJK text renders as boxes | Confirm `platform_get_glyph` uses `mat.eM11.value = 1` (not `mat.eM11.fract = 1`). |
| Pet invisible | Window is 160×160; only the bottom 96×96 is the pet. Move the mouse to the bottom-right corner. |
| LLM never fires | Check `llm_enabled` log line at startup; empty `api_key` disables it. |
| WinHTTP appears to hang | Code uses `WINHTTP_ACCESS_TYPE_NO_PROXY`. If you need a proxy, switch the flag in `platform_win32.c`. |
