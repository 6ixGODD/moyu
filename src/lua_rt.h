#pragma once
#include "loop.h"

#include <stdbool.h>

struct lua_State;

// Create a sandboxed Lua state, register moyu.* API, load the default script.
// `app` is stored in the registry and used by all moyu.* bindings.
struct lua_State* lua_runtime_create(moyu_app* app, const char* script_path);

void lua_runtime_destroy(struct lua_State* L);

// Call optional Lua function `on_tick(idle_s, mouse_dist, valence, arousal)`.
// Safe no-op if not defined.
void lua_runtime_call_on_tick(moyu_app* app,
                              float idle_s,
                              float mouse_dist,
                              float valence,
                              float arousal);

// Call optional Lua function `on_click(button)`.
void lua_runtime_call_on_click(moyu_app* app, int button);
void lua_runtime_call_on_event(moyu_app* app,
                               const char* event_name,
                               const char* payload_json);
bool lua_runtime_propose_desire(moyu_app* app,
                                const char* state_json,
                                char* goal,
                                size_t goal_cap,
                                char* tool,
                                size_t tool_cap,
                                char* args_json,
                                size_t args_cap);
void lua_runtime_call_tool_result(moyu_app* app, const char* result_json);
void lua_runtime_call_memory_candidate(moyu_app* app, const char* summary);
