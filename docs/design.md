# Design Notes

This document captures the *why* behind MOYU's structure. The *what* is
in [architecture.md](architecture.md).

## 1. Only platform code is abstracted

The TODO spec said: abstract the platform, nothing else. We took this
literally. There is no "renderer interface," no "agent base class," no
"tool provider registry pattern." There is one `platform.h` and a bunch
of free functions that operate on plain structs.

**Why.** Every abstraction layer is a stack frame on the hot path and a
file a new contributor has to read before they can change anything. A
desktop pet's hot path is rendering + event polling at 1 Hz — there is
no business logic complex enough to justify indirection. The closest
thing to "complex" is the LLM round-trip, and that is dominated by
network latency, not call overhead.

**Cost.** Adding a new animation is a switch-case in `procedural.c`,
not a virtual method override. Adding a new tool is a `tool_def`
literal in `main.c`. Both are fine.

## 2. Flat `src/` directory

The original plan nested sources into `src/{util,platform,render,core,
llm,tools,script}/`. We flattened it to a single `src/` with short
filenames (`llm.c` not `llm/completion.c`, `lua_rt.c` not
`script/lua_runtime.c`).

**Why.** C projects with a healthy "C smell" — musl, Redis, SQLite —
tend to have flat or shallow trees. Nesting suggests a framework;
flatness suggests a program. The include paths also shorten (`"llm.h"`
vs `"llm/completion.h"`), which matters because C headers are visible
in every file that uses them.

**Cost.** With ~20 files the directory is still scannable. Past ~50 we
would reconsider.

## 3. Single-threaded, synchronous LLM

The TODO spec mentioned an "LLM worker thread with condition variable."
We did not implement it. LLM calls block the main loop.

**Why.** The pet's UI is a 1 Hz tick — blocking for 500 ms during an LLM
call is invisible to the user (the sprite just holds its current frame
for one extra tick). A worker thread would add: a queue, a mutex, a
condition variable, join logic at shutdown, and a class of
use-after-free bugs when the Lua state is touched from the wrong
thread. None of that buys us anything at 1 Hz.

**Cost.** If we ever raise the tick rate or add streaming LLM, we will
need to revisit. The platform boundary is clean enough that moving to a
worker thread later touches only `loop.c`.

## 4. JSON everywhere in the context store

`context_node` stores `input`/`output`/`metadata` as JSON strings, not
typed fields. This looks wasteful (parse on read, serialise on write)
but:

- The same node is sent to the LLM verbatim — no conversion needed.
- The same node is logged verbatim — no conversion needed.
- MCP tools already speak JSON — no conversion needed.
- Adding a new field is a schema change in the producer, not a struct
  change in `context.h`.

**Cost.** CPU is spent on cJSON parse/serialise. At 1 Hz with a 64-node
ring, this is unmeasurable.

## 5. Procedural sprites, no asset files

The 6 animations (idle/blink/sleep/happy/sad/observe) are generated in
code by `procedural.c` — 32×32 RGBA, 4 frames each. No PNG, no BMP, no
zlib.

**Why.** Zero external dependencies, zero decoding bugs, ~0 bytes on
disk. The pet is intentionally low-fidelity pixel art; procedural
generation matches the aesthetic.

**Cost.** The art is what it is. Replacing it later with BMPs is a
matter of writing a 50-line BMP loader and pointing `sprite.c` at files.

## 6. Lua sandbox, not a configuration file

Behaviour rules live in `scripts/default.lua`, not `config.json`. JSON
config carries only static values (API key, personality floats, rule
thresholds). Anything with a branch lives in Lua.

**Why.** JSON is data. Behaviour is code. Conflating them produces
Turing-incomplete config DSLs that grow until they reimplement Lua badly.

**Cost.** Users editing behaviour need to learn the small `moyu.*` API.
The sandbox surface is ~10 functions — smaller than most config schemas.

## 7. One binary, no installer

`moyu.exe` plus `assets/` and `scripts/` next to it. That is the entire
deployment. No registry entries, no `%APPDATA%`, no service.

**Why.** A desktop pet is not a database. State that needs to survive
restarts (personality drift, say) can be persisted to a JSON file next
to the exe in a future iteration; for v0.1, state is in-memory and
resets on exit, which matches the "occasional companion" feel.

## 8. What we deliberately did not do

- **No unit tests.** Behaviour is verified by running the pet. Adding
  `tests/` is a TODO once the API stabilises.
- **No streaming LLM.** Synchronous only.
- **No tool-use loop.** The LLM cannot call tools autonomously; Lua
  decides. This is per the spec's "90% rule-based" requirement.
- **No persistence.** Personality and context reset on restart.
- **No auto-update.** It is a 600 KB binary; replace the file.
- **No Linux/macOS runtime.** Stubs compile; full implementation is a
  platform-port task that does not require touching any non-platform
  file.
