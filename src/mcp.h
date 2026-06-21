#pragma once

#include "tool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  MCP_TRANSPORT_HTTP = 0,
  MCP_TRANSPORT_STDIO,
} mcp_transport;

typedef struct {
  char* name;
  char* description;
  char* input_schema;
} mcp_tool_info;

typedef struct mcp_client {
  char* name;
  mcp_transport transport;
  char* server_url;
  char* api_key;
  char* command_line;
  char* working_dir;
  char* session_id;
  char protocol_version[24];
  bool connected;
  int next_id;
  mcp_tool_info* tools;
  size_t tool_count;
#ifdef _WIN32
  void* process;
  void* thread;
  void* stdin_write;
  void* stdout_read;
#endif
} mcp_client;

void mcp_client_init_http(mcp_client* c,
                          const char* name,
                          const char* server_url,
                          const char* api_key);
void mcp_client_init_stdio(mcp_client* c,
                           const char* name,
                           const char* command_line,
                           const char* working_dir);
// Backward-compatible alias for old URL-only config.
void mcp_client_init(mcp_client* c,
                     const char* server_url,
                     const char* api_key);
void mcp_client_free(mcp_client* c);

bool mcp_connect(mcp_client* c);
bool mcp_discover(mcp_client* c);
char* mcp_call(mcp_client* c,
               const char* tool_name,
               const char* arguments_json);
bool mcp_register_tools(mcp_client* c, tool_registry* reg);
