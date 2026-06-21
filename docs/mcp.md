# MCP and Tools

## Supported transports

MOYU implements MCP tools over:

- stdio child processes with newline-delimited JSON-RPC
- Streamable HTTP POST with JSON or SSE `data:` responses

The client sends MCP protocol/version and Accept headers, stores `MCP-Session-Id`, performs initialize/initialized, discovers tools, and invokes tools. Protocol target is `2025-11-25`; the server's negotiated version is retained.

## Configuration

stdio:

```json
{
  "name": "git-local",
  "transport": "stdio",
  "command": "D:\\apps\\moyu-mcp-git.exe",
  "args": [],
  "cwd": "D:\\WorkSpace"
}
```

Streamable HTTP:

```json
{
  "name": "weather",
  "transport": "streamable_http",
  "url": "http://127.0.0.1:7788/mcp",
  "api_key": ""
}
```

Legacy entries containing only `url` are treated as Streamable HTTP.

## Tool metadata

Every registered tool has name, description, input schema, source, affordance and risk. Names containing publish/write/delete/remove are conservatively classified mutate; create/draft are classified draft; other discovered tools default observe.

Lua may invoke builtin observe/draft tools. External tools require a durable tool/scope permission. Human chat can explicitly invoke a tool:

```text
/allow git.recent_commits *
/tool git.recent_commits {"root":"D:\\repo"}
```

## Builtins

Builtins cover time, idle time, optionally foreground process, pet expression, memory, collections, drafts, runtime explain/cancel and bounded filesystem metadata observation. User-file deletion/move/write is not built in.

## Reference servers

`build.bat` and CMake produce:

- `moyu-mcp-git.exe`: stdio, recent commits/stale branches/TODO candidates
- `moyu-mcp-notes.exe`: stdio, create draft/publish
- `moyu-mcp-weather.exe`: Streamable HTTP on `127.0.0.1:7788`, Open-Meteo current/forecast

They are testable examples and do not start by default.

## Limits

Resources, prompts, sampling, elicitation and OAuth are not implemented. Streamable HTTP currently uses POST request/response sessions; server-initiated standalone GET streams are outside v0.2.
