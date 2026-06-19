# Context System

The context system is the agent's only state representation. Every
meaningful event — a user interaction, a tool call, an LLM reflection, an
idle tick — becomes one `context_node` in a rolling ring buffer.

## Data model

```c
typedef enum {
  CTX_IDLE = 0,
  CTX_INTERACTION,
  CTX_TOOL,
  CTX_REFLECTION,
} context_type;

typedef struct {
  uint64_t id;
  context_type type;
  char* input;     // JSON string (owned)
  char* output;    // JSON string (owned, may be NULL)
  char* metadata;  // JSON string (owned, may be NULL)
  uint64_t ts_ms;
} context_node;

typedef struct {
  context_node* ring;  // cap slots
  size_t cap;          // max nodes kept (default 64)
  size_t head;         // next write index
  size_t count;        // current count (≤ cap)
  uint64_t next_id;    // monotonic
} context_store;
```

- Each node carries **JSON strings** for input/output/metadata. JSON is
  the canonical wire format — it survives serialization to LLM prompts,
  MCP calls, and log files without extra conversion.
- Strings are owned by the node (heap-copied on push, freed on overwrite).
- The store is a ring buffer: writing past `cap` overwrites the oldest
  node. This bounds memory without GC pauses.

## Operations

| Function | Purpose |
|---|---|
| `context_push(s, type, input, output, metadata)` | Append a node. Strings copied. |
| `context_last_of(s, type)` | Newest node of a given type, or NULL. |
| `context_at(s, reverse_index)` | Iterate newest-first (0 = newest). |
| `context_hash(s)` | FNV-1a over the rolling window — used as LLM cache key. |
| `context_to_json(s, last_n)` | Flat JSON array of the last N nodes for LLM prompts. |

## Why a ring buffer, not a list?

1. **Bounded memory.** A pet runs for days; an unbounded log would OOM.
2. **No allocation on the hot path.** Writes reuse existing slots.
3. **Cache key stability.** `context_hash` is computed over the visible
   window only, so identical recent histories produce identical cache
   keys — even if ancient nodes differ.

## How nodes are produced

| Type | When | Who pushes |
|---|---|---|
| `CTX_IDLE` | Boot, periodic idle ticks | `loop.c` |
| `CTX_INTERACTION` | Mouse hover, click | `loop.c` |
| `CTX_TOOL` | Built-in or MCP tool invoked | `tool.c` / `mcp.c` |
| `CTX_REFLECTION` | LLM completion returned | `loop.c` (after `llm_complete`) |

## Composing for an LLM prompt

When the runtime decides to consult the LLM, it calls `context_to_json(s,
last_n)` to produce a flat array of the most recent nodes. This JSON is
embedded into the `messages` array sent to the chat completions endpoint.
The LLM sees the pet's recent history verbatim — no templating, no lossy
summarisation at this layer. Summarisation, if ever needed, belongs in a
future `CTX_SUMMARY` node type that a Lua hook can emit.

## Cache interaction

`context_hash` feeds `llm_cache_get` / `llm_cache_put`. If the rolling
window has not changed since the last call with an identical prompt, the
cached response is returned without an HTTP round-trip. This makes the
"occasional LLM-driven micro narrative" essentially free when nothing is
happening.
