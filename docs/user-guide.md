# MOYU User Guide

Chinese version: [user-guide.zh-CN.md](user-guide.zh-CN.md)

## What MOYU actually does

MOYU consists of two cooperating programs:

- `moyu.exe` is the lightweight desktop creature and autonomous runtime.
- `moyu-chat.exe` is the interactive terminal used to talk to and control the same creature.

Both processes share `~/.moyu/`. Closing chat does not stop the pet. Restarting either process does not erase personality, memory, permissions, unfinished intentions, collections, or drafts.

Implemented capabilities:

| Capability | What it means | Where to see or control it |
| --- | --- | --- |
| Soul | Fixed identity, principles, and behavioral boundaries loaded on every start | `~/.moyu/SOUL.md` |
| Long-term memory | Human-readable facts and promoted experiences; can be edited by hand | `MEMORY.md`, `/memory`, `/remember`, `/forget` |
| Episodic state | Conversations, discoveries, beliefs, feedback, intentions, habits, permissions, and budgets | `state.db`, `/status` |
| Agent runtime | One persistent intention at a time, at most three steps, deadline and budget enforcement | `/status`; ask “what are you doing?” |
| Human shaping | Corrections reduce belief confidence; remember/forget and permission decisions change later behavior | natural conversation and TUI commands |
| Authorized observation | Lists metadata under one approved root; resolves final paths to prevent junction escape | onboarding, `filesystem.observe` |
| Collections | Durable Markdown cards for discoveries worth keeping, echoed back into the desktop UI | `/collect`, drag-and-drop, `collections/`, hover card |
| Drafts | Private Markdown output that cannot publish itself | `/draft`, `/drafts`, `drafts/` |
| MCP | Discovers and calls tools from stdio and Streamable HTTP servers | `config.json`, `/tools`, `/tool` |
| LLM chat | Uses SOUL, MEMORY and relevant SQLite context with the configured OpenAI-compatible model | type normally in `moyu-chat.exe` |
| Vision analysis | Uses a separate multimodal model to inspect dropped images | `vision` config, image drag-and-drop |
| Offline mode | Memory, collections, drafts, permissions and runtime state remain usable without an LLM | all local slash commands |

Collections are not a browser bookmark folder. They are MOYU's durable findings: for example, an unusual repository, a recurring work pattern, or something the human explicitly asks it to keep. Each item is a readable Markdown file. When a collection changes, MOYU reflects that back into the desktop GUI through an info card and a remembered "latest kept" line. Drafts are proposed text that remains private until a human deliberately uses it elsewhere.

## Start and interact

Run the desktop creature:

```bat
build\moyu.exe
```

Double-click the creature to open a compact pixel-style prompt beside it. The prompt is clamped to the active monitor, starts at one text line, grows up to five lines, and then enables its internal scrollbar. The native text area retains IME, selection, undo, and clipboard behavior; use the round check button to send.

For a full session, right-click the tray icon and select **Open Terminal Chat**. You can also start it directly:

```bat
build\moyu-chat.exe
```

The Windows tray icon exposes a native context menu that follows the system light/dark theme. It deliberately keeps only system-level actions: open terminal chat, open collections, or quit MOYU. Right-clicking the creature opens a native status window with explicit **Talk to MOYU...** and **Open collected items** actions. Speech bubbles and info cards do not take focus or stack on top of each other.

MOYU has 20 built-in procedural actions. Its scheduler combines curiosity, patience, sarcasm, valence, and arousal to choose roaming, sneaking toward the pointer, peeking, boredom, playful feints, stretching, or yawning. Bringing the pointer too close drives it away; a cheeky personality may tease and dodge instead of looking frightened.

Extra GUI interaction:

- Drag a file or folder onto MOYU and it will keep a collection note about that drop, then react visually.
- Dropped text/code files are previewed locally and then summarized by the main model, so this does not depend on provider-side file parsing support.
- Dropped images can be analyzed through the separate `vision` model configuration.
- Hold and drag MOYU to move it to a new perch on the desktop.
- Repeated pats trigger different emotional feedback instead of the same click loop every time.
- Mouse-near sensing, drag-state feedback, and the companion panel make the desktop side feel more alive than a static tray utility.

## Terminal chat

Normal input is sent to the configured LLM with current SOUL, MEMORY and relevant runtime state. Slash commands execute locally and are always available:

```text
/help
/status
/config
/model qwen-plus
/baseurl https://dashscope.aliyuncs.com/compatible-mode/v1
/apikey sk-...
/pause
/resume
/cancel
/memory
/remember I prefer quiet notifications
/forget quiet notifications
/collections
/collect Interesting repository | A tiny C project worth revisiting.
/drafts
/draft Release note | Explain the new terminal chat.
/tools
/allow git.recent_commits *
/tool git.recent_commits {"root":"D:\\WorkSpace\\project"}
/clear
/exit
```

Forgetting asks for an explicit `YES`. Calling a mutate-class MCP tool asks for `RUN`. This prevents a pasted command or generated text from silently changing an external system.

## Work directory

```text
~/.moyu/
  SOUL.md             fixed identity; MOYU never edits it
  MEMORY.md           human-readable long-term memory
  MEMORY.md.bak       previous atomic memory version
  config.json         LLM, MCP, privacy and autonomy configuration
  secrets.dat         Windows DPAPI-protected LLM key
  state.db            structured runtime state and audit trail
  collections/*.md    durable findings
  drafts/*.md         private drafts
  logs/               runtime logs
```

Editing `SOUL.md` or `MEMORY.md` changes subsequent sessions. Restart reloads both; `/memory` reloads MEMORY immediately. `config.json` is loaded from this directory at startup, not compiled into the binaries. The memory writer rejects common credential patterns, and `/apikey` stores the LLM key in `secrets.dat` through Windows DPAPI instead of plaintext config. `config.json` now also supports `vision`, `owner`, and `appearance.skin`.

## MCP and built-in behavior

MCP servers are configured in `~/.moyu/config.json`. MOYU supports lifecycle initialization, tool discovery, ping and tool calls over local stdio or Streamable HTTP. `/tools` shows source and risk classification.

Three reference servers are built:

- `moyu-mcp-git.exe`: repository status, recent commits and branches through stdio.
- `moyu-mcp-notes.exe`: draft creation and explicit publishing through stdio.
- `moyu-mcp-weather.exe`: weather lookup through local Streamable HTTP, backed by Open-Meteo.

The runtime also has built-in time, idle time, foreground application (permission required), memory, collection, draft, runtime explain/cancel, and bounded filesystem observation tools. Built-ins are used by the autonomous runtime; common human operations have dedicated TUI commands.

See [mcp.md](mcp.md) for exact configuration and protocol boundaries.

## Privacy and recovery

- MOYU initially sees only its own home. Observation outside it requires an approved root.
- Observation reads names, sizes and directory flags, not arbitrary user-file contents.
- `observe` can be persistently allowed; `draft` only writes private draft state; `mutate` requires explicit human invocation and confirmation.
- Secrets are excluded from memory, logs, prompts where practical, and SQLite tool audit payloads.
- A corrupt database is preserved as `state.db.corrupt-<timestamp>` before a clean schema is created.
- Interrupted Markdown writes preserve the original and its backup.
- Expired intentions fail after restart instead of replaying a mutation.

## Current scope

Windows is the fully supported v0.2 platform. Linux and macOS platform files remain stubs. MCP resources, prompts, sampling, OAuth, and a standalone HTTP GET event stream are not implemented. The terminal is intentionally native and dependency-free: it supports UTF-8, ANSI color, persistent context, local commands and explicit tool execution, but is not a shell or source-code editor.
