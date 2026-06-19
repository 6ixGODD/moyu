#include "mcp.h"
#include "../util/mem.h"
#include "../util/log.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

void mcp_client_init(mcp_client* c, const char* server_url, const char* api_key) {
    memset(c, 0, sizeof(*c));
    c->server_url = moyu_strdup(server_url);
    c->api_key = api_key ? moyu_strdup(api_key) : NULL;
    c->connected = false;
}

void mcp_client_free(mcp_client* c) {
    if (!c) return;
    if (c->server_url) moyu_free(c->server_url);
    if (c->api_key)    moyu_free(c->api_key);
    for (size_t i = 0; i < c->tool_count; i++) moyu_free(c->tool_names[i]);
    if (c->tool_names) moyu_free(c->tool_names);
    memset(c, 0, sizeof(*c));
}

static char* build_rpc(const char* method, cJSON* params, int id) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method);
    if (params) cJSON_AddItemToObject(root, "params", params);
    else cJSON_AddNullToObject(root, "params");
    cJSON_AddNumberToObject(root, "id", id);
    char* s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

// Adapter: turn an MCP tool into a tool_def whose invoke proxies back to mcp_call.
typedef struct {
    mcp_client* client;
    char* tool_name;
} mcp_tool_adapter;

static char* mcp_tool_invoke(const char* input_json, void* user) {
    mcp_tool_adapter* a = (mcp_tool_adapter*)user;
    if (!a) return NULL;
    char* res = mcp_call(a->client, a->tool_name, input_json);
    return res;
}

// We store adapters in a side-table owned by the mcp_client so we can free them later.
// (For MVP we just leak them; the client lives for program lifetime.)

bool mcp_discover(mcp_client* c) {
    char* body = build_rpc("tools/list", NULL, 1);
    platform_http_resp r = platform_http_post_json(c->server_url, c->api_key ? c->api_key : "", body, 10000);
    moyu_free(body);
    if (r.err) {
        LOGW("MCP discover: %s", r.err);
        platform_http_resp_free(&r);
        return false;
    }
    if (r.status != 200) {
        LOGW("MCP discover: HTTP %d", r.status);
        platform_http_resp_free(&r);
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(r.body, r.body_len);
    if (!root) { platform_http_resp_free(&r); return false; }
    cJSON* result = cJSON_GetObjectItem(root, "result");
    cJSON* tools = result ? cJSON_GetObjectItem(result, "tools") : NULL;
    if (!tools || !cJSON_IsArray(tools)) {
        cJSON_Delete(root);
        platform_http_resp_free(&r);
        return false;
    }
    // free old
    for (size_t i = 0; i < c->tool_count; i++) moyu_free(c->tool_names[i]);
    if (c->tool_names) moyu_free(c->tool_names);
    c->tool_count = cJSON_GetArraySize(tools);
    c->tool_names = (char**)moyu_alloc(c->tool_count * sizeof(char*));
    for (size_t i = 0; i < c->tool_count; i++) {
        cJSON* t = cJSON_GetArrayItem(tools, (int)i);
        cJSON* name = cJSON_GetObjectItem(t, "name");
        c->tool_names[i] = moyu_strdup(name && cJSON_IsString(name) ? name->valuestring : "unnamed");
    }
    c->connected = true;
    cJSON_Delete(root);
    platform_http_resp_free(&r);
    return true;
}

char* mcp_call(mcp_client* c, const char* tool_name, const char* arguments_json) {
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", tool_name);
    // arguments is expected to be a JSON object; parse and re-attach
    cJSON* args = cJSON_Parse(arguments_json ? arguments_json : "{}");
    if (!args) args = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "arguments", args);
    char* body = build_rpc("tools/call", params, 2);
    platform_http_resp r = platform_http_post_json(c->server_url, c->api_key ? c->api_key : "", body, 30000);
    moyu_free(body);
    if (r.err || r.status != 200) {
        platform_http_resp_free(&r);
        return NULL;
    }
    // Return the raw result JSON (caller decides how to interpret)
    char* out = moyu_strdup(r.body ? r.body : "");
    platform_http_resp_free(&r);
    return out;
}

bool mcp_register_tools(mcp_client* c, tool_registry* reg) {
    if (!c->connected && !mcp_discover(c)) return false;
    for (size_t i = 0; i < c->tool_count; i++) {
        mcp_tool_adapter* a = (mcp_tool_adapter*)moyu_alloc(sizeof(*a));
        a->client = c;
        a->tool_name = moyu_strdup(c->tool_names[i]);
        tool_def def = {0};
        def.name = a->tool_name;
        def.description = "MCP tool";
        def.input_schema_json = "{}";
        def.invoke = mcp_tool_invoke;
        def.user = a;
        tool_registry_add(reg, def);
    }
    return true;
}
