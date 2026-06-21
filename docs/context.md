# Memory and Context

## Files

MOYU owns one persistent directory:

```text
~/.moyu/
  SOUL.md       immutable to MOYU, editable by the human
  MEMORY.md     shared, readable long-term narrative memory
  state.db      transactional runtime state
  secrets.dat   DPAPI-protected API key on Windows
  config.json
  collections/
  drafts/
  logs/
```

Missing files are created once and never overwrite human edits.

## SOUL.md

SOUL defines identity, honesty, privacy and behavioral boundaries. Every LLM request receives SOUL before other context. MOYU never writes this file.

## MEMORY.md

MEMORY has controlled sections for the human, habits, beliefs, episodes and obsessions. Writes are limited to 64 KiB, deduplicated, filtered for common secret patterns, written to a temporary file and atomically replaced. The previous version is kept as `.bak`.

`memory_consider_episode` combines importance, novelty and personality. Only eligible episodes enter a deterministic probability gate. “记住” writes directly after validation. “忘掉” requires confirmation and removes matching list entries.

## SQLite

Schema v1 stores episodes, beliefs, drives, habits, relationships, intentions/steps, permissions, tool audit, MCP metadata, chats, inbox, collections and daily budgets. Ordinary history is retained for 90 days; promoted memory, permissions and active state are not automatically removed.

## Prompt compose

LLM context order is fixed:

```text
system safety instruction
SOUL.md
MEMORY.md
active intention
high-weight durable beliefs/episodes
current input
```

The composed input is bounded to 24,000 characters. Raw databases, complete tool responses and credentials are never inserted.

## Short-term context

The existing 64-node ring remains a bounded session working memory and cache input. It is no longer the sole state representation.
