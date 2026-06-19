#pragma once
#include "platform.h"
#include "tool.h"

#include <stdbool.h>

// Minimal MCP (Model Context Protocol) client.
// MVP: HTTP transport only. Discovers tools via tools/list, calls via tools/call.
// JSON-RPC 2.0 over HTTP POST.

typedef struct {
  char* server_url;  // owned
  char* api_key;     // owned, optional
  bool connected;
  // Last-seen tool names from this server
  char** tool_names;
  size_t tool_count;
} mcp_client;

void mcp_client_init(mcp_client* c,
                     const char* server_url,
                     const char* api_key);
void mcp_client_free(mcp_client* c);

// Discover tools; populates c->tool_names. Returns true on success.
bool mcp_discover(mcp_client* c);

// Call a tool. Returns owned JSON result string (caller frees) or NULL.
char* mcp_call(mcp_client* c,
               const char* tool_name,
               const char* arguments_json);

// Register all tools from this MCP server into the registry (dynamic loading).
// Each MCP tool becomes a tool_def whose invoke proxies back to mcp_call.
bool mcp_register_tools(mcp_client* c, tool_registry* reg);
