# MCP (Model Context Protocol)

MOYU ships with a minimal MCP client so the pet can call external tools
exposed by any MCP server speaking HTTP transport.

## What MCP adds

Without MCP, the pet has three built-in tools (`say`, `poke`,
`mouse_distance`) implemented in C. MCP lets you add arbitrarily many
more — filesystem search, web fetch, calendar, home automation — without
recompiling. Tools are discovered at startup and become first-class
entries in the same `tool_registry` the built-ins use.

## Configuration

Add servers to `assets/config.json`:

```json
{
  "mcp_servers": [
    {
      "url": "https://my-mcp-server.example.com/rpc",
      "api_key": "optional-bearer-token"
    }
  ]
}
```

At boot, `main.c` iterates this list, creates an `mcp_client` per entry,
calls `mcp_register_tools`, and merges the discovered tools into the
shared `tool_registry`. Each MCP tool becomes a `tool_def` whose `invoke`
proxies back to `mcp_call` over HTTP.

## Wire protocol

MCP over HTTP is JSON-RPC 2.0:

**Discovery** — `tools/list`:
```http
POST /rpc
Content-Type: application/json

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/list"
}
```

Response:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "tools": [
      {
        "name": "search_web",
        "description": "Search the public web.",
        "inputSchema": { "type": "object", "properties": { "q": { "type": "string" } } }
      }
    ]
  }
}
```

**Invocation** — `tools/call`:
```http
POST /rpc
Content-Type: application/json

{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
    "name": "search_web",
    "arguments": { "q": "moyu desktop pet" }
  }
}
```

## How tools are exposed to the LLM

Today, MCP tools are callable from Lua via the same `moyu.*` plumbing
that built-ins use — they appear in the registry, and the runtime can
dispatch to them by name. Future work: a `moyu.tool(name, args)` binding
so Lua scripts can call any registered tool directly.

The LLM does not currently drive tool calling autonomously. The
intentional design is that Lua rules decide *when* to call a tool —
keeping the LLM as a reflective narrator, not an autonomous agent. This
matches the TODO.md spec: 90% rule-based, 10% LLM.

## Limits of the MVP

- **HTTP transport only.** stdio transport (used by some MCP servers like
  the official filesystem server) is not implemented. To use those, run
  them behind an HTTP bridge.
- **No streaming.** Each call is one request → one response.
- **No auth refresh.** The `api_key` is sent as a static Bearer token.
  OAuth/token-rotation is out of scope for v0.1.
- **No reconnection.** If a server is unreachable at boot, its tools are
  simply absent; there is no retry loop.
- **Lifetime.** `mcp_client` structs are allocated once at boot and live
  for the process lifetime. Disconnecting a server at runtime is a TODO.

## Writing an MCP server

Any HTTP endpoint that answers the two JSON-RPC methods above works. A
~50-line Python `aiohttp` server or a static nginx config returning a
fixed tool list is enough to experiment. See the MCP spec at
https://modelcontextprotocol.io for the full schema.
