# Design Decisions

## A creature, not a chatbot

Chat is the place to ask, correct and grant permission. The recurring product loop is the creature's durable belief, habit, collection and unfinished project. Ordinary conversation does not automatically become a tool task.

## BDI with hard limits

MOYU uses a small Belief-Desire-Intention model instead of an unlimited ReAct loop. C owns validation, permissions, budgets and transitions. Lua expresses policy. The LLM may explain, condense or suggest but cannot bypass the scheduler.

Only one intention may be active. The default runtime allows at most three steps, one optional LLM call and a 30-second deadline. This bounds cost, recovery behavior and surprise.

## Memory is editable

Long-term identity and narrative memory are Markdown, not opaque vectors. The human can inspect and correct both. SQLite stores transactional facts, not the canonical prose memory. No embedding service or vector database is required.

Automatic memory is intentionally selective: importance, novelty and personality produce a promotion chance. Explicit “remember” bypasses chance; sensitive-looking text is rejected.

## Permissions are part of the relationship

Tools are classified as observe, draft or mutate. Scope decisions are durable and auditable. Unknown external tools cannot become autonomous senses until a human grants permission and an affordance exists. Mutations require an explicit human command.

## One blocking worker

The old synchronous LLM froze the transparent window. A single sleeping worker removes that failure without creating a general task framework. It adds no idle polling and keeps Lua single-threaded.

## Programmatic visual identity

The cream spirit uses code-generated pixels rather than generated art or a decoder dependency. The result is deterministic, tiny, themeable through Lua/BMP, and aligned with the simple line-art requirement.

## Deliberate limits

- Windows complete; other desktop ports deferred.
- MCP tools only; resources/prompts/sampling/OAuth deferred.
- No irreversible autonomous actions.
- No continuous environment surveillance.
- No auto-update or marketplace in v0.2.
