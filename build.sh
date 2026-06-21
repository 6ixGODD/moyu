#!/usr/bin/env bash
# Direct clang build (no CMake required). Targets Windows (Win32) when run on win32.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
mkdir -p "$BUILD"

CC=${CC:-clang}
PLATFORM=$(uname -s 2>/dev/null || echo Windows)
case "$PLATFORM" in
    MINGW*|MSYS*|CYGWIN*|Windows*) HOST=win32 ;;
    Darwin) HOST=macos ;;
    *) HOST=linux ;;
esac

LUA_SRC=$(ls "$ROOT/third_party/lua"/*.c)
CJSON_SRC="$ROOT/third_party/cjson/cJSON.c"
SQLITE_SRC="$ROOT/third_party/sqlite/sqlite3.c"

MOYU_SRC=$(find "$ROOT/src" -name '*.c' | sort)
if [ "$HOST" = "win32" ]; then
    MOYU_SRC=$(echo "$MOYU_SRC" | grep -v platform_linux | grep -v platform_macos)
elif [ "$HOST" = "macos" ]; then
    MOYU_SRC=$(echo "$MOYU_SRC" | grep -v platform_linux | grep -v platform_win32)
else
    MOYU_SRC=$(echo "$MOYU_SRC" | grep -v platform_win32 | grep -v platform_macos)
fi

WARN="-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-missing-field-initializers"
CFLAGS="-std=c11 -O2 -D_CRT_SECURE_NO_WARNINGS -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 $WARN -I$ROOT/src -I$ROOT/third_party/lua -I$ROOT/third_party/cjson -I$ROOT/third_party/sqlite"

if [ "$HOST" = "win32" ]; then
    OUT="$BUILD/moyu.exe"
    LINK="-lwinhttp -luser32 -lgdi32 -lshell32 -lole32 -lcrypt32 -lws2_32"
    LDFLAGS="-Wl,-subsystem,windows"
    echo "[build] host=win32 cc=$CC"
    # shellcheck disable=SC2086
    $CC $CFLAGS $MOYU_SRC $LUA_SRC $CJSON_SRC $SQLITE_SRC $LINK $LDFLAGS -o "$OUT"
else
    OUT="$BUILD/moyu"
    if [ "$HOST" = "macos" ]; then
        LINK="-framework AppKit -framework Foundation"
    else
        LINK="-lcurl -lX11"
    fi
    echo "[build] host=$HOST cc=$CC"
    # shellcheck disable=SC2086
    $CC $CFLAGS $MOYU_SRC $LUA_SRC $CJSON_SRC $SQLITE_SRC $LINK -o "$OUT"
fi

# Copy assets & scripts next to binary
cp -r "$ROOT/assets" "$BUILD/" 2>/dev/null || true
cp -r "$ROOT/scripts" "$BUILD/" 2>/dev/null || true

echo "[build] -> $OUT"
ls -la "$OUT"
