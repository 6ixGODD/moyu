#include "lua_rt.h"

#include "lauxlib.h"
#include "log.h"
#include "lua.h"
#include "lualib.h"
#include "agent.h"
#include "memory.h"
#include "mem.h"
#include "platform.h"
#include "state.h"
#include "tool.h"

#include <string.h>

static const char* APP_KEY = "moyu_app";

static moyu_app* get_app(lua_State* L) {
  lua_pushstring(L, APP_KEY);
  lua_rawget(L, LUA_REGISTRYINDEX);
  moyu_app* app = (moyu_app*)lua_touserdata(L, -1);
  lua_pop(L, 1);
  return app;
}

static int l_idle_seconds(lua_State* L) {
  moyu_app* app = get_app(L);
  uint64_t now = platform_now_ms();
  uint64_t idle =
      (now > app->last_mouse_move_ms) ? (now - app->last_mouse_move_ms) : 0;
  lua_pushnumber(L, (lua_Number)idle / 1000.0);
  return 1;
}

static int l_mouse_distance(lua_State* L) {
  moyu_app* app = get_app(L);
  lua_pushnumber(L, (lua_Number)app->last_mouse_distance);
  return 1;
}

static int l_mood(lua_State* L) {
  moyu_app* app = get_app(L);
  lua_pushnumber(L, (lua_Number)app->emotion.valence);
  lua_pushnumber(L, (lua_Number)app->emotion.arousal);
  return 2;
}

static int l_personality(lua_State* L) {
  moyu_app* app = get_app(L);
  lua_pushnumber(L, (lua_Number)app->personality.mood_bias);
  lua_pushnumber(L, (lua_Number)app->personality.sarcasm);
  lua_pushnumber(L, (lua_Number)app->personality.curiosity);
  lua_pushnumber(L, (lua_Number)app->personality.patience);
  return 4;
}

static int l_emit(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  const char* payload = lua_isstring(L, 2) ? lua_tostring(L, 2) : NULL;
  moyu_app* app = get_app(L);
  event_push_action(&app->events, name, payload);
  return 0;
}

static int l_say(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  int dur = (int)luaL_optinteger(L, 2, 5000);
  moyu_app* app = get_app(L);
  moyu_app_emit_say(app, text, dur);
  return 0;
}

static int l_anim(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  moyu_app* app = get_app(L);
  int id = anim_id_from_name(name);
  moyu_app_emit_anim(app, id);
  return 0;
}

static int l_llm(lua_State* L) {
  const char* prompt = luaL_checkstring(L, 1);
  moyu_app* app = get_app(L);
  bool ok = moyu_app_request_llm(app, prompt);
  if (!ok) {
    lua_pushnil(L);
    return 1;
  }
  if (app->say_text)
    lua_pushstring(L, app->say_text);
  else
    lua_pushnil(L);
  return 1;
}

static int l_log(lua_State* L) {
  const char* msg = luaL_checkstring(L, 1);
  LOGI("[lua] %s", msg);
  return 0;
}

static int l_tool(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  const char* args = luaL_optstring(L, 2, "{}");
  moyu_app* app = get_app(L);
  const tool_def* def = tool_registry_find(app->tools, name);
  if (!def || def->risk == TOOL_MUTATE) {
    lua_pushnil(L);
    lua_pushstring(L, def ? "mutating tools require human confirmation"
                          : "unknown tool");
    return 2;
  }
  if (strcmp(def->source, "builtin") != 0 &&
      !state_permission_allowed(
          app->state, def->name, "*", tool_risk_name(def->risk))) {
    state_add_inbox(app->state,
                    "request",
                    "A new sense needs permission",
                    def->name);
    lua_pushnil(L);
    lua_pushstring(L, "permission required");
    return 2;
  }
  char* out = tool_invoke(def, args);
  if (!out) {
    lua_pushnil(L);
    lua_pushstring(L, "tool failed");
    return 2;
  }
  state_add_episode(app->state, "tool", def->name, "{}", 0.35, 0.30);
  lua_pushstring(L, out);
  moyu_free(out);
  return 1;
}

static int l_beliefs(lua_State* L) {
  moyu_app* app = get_app(L);
  char* json = state_relevant_context(app->state, 16);
  lua_pushstring(L, json);
  moyu_free(json);
  return 1;
}

static int l_intention(lua_State* L) {
  moyu_app* app = get_app(L);
  char* json = agent_explain(app->agent);
  lua_pushstring(L, json);
  moyu_free(json);
  return 1;
}

static int l_propose_memory(lua_State* L) {
  const char* text = luaL_checkstring(L, 1);
  moyu_app* app = get_app(L);
  int64_t id = state_add_episode(
      app->state, "memory_candidate", text, "{\"src\":\"lua\"}", 0.55, 0.50);
  bool stored = memory_consider_episode(app->memory,
                                        id,
                                        text,
                                        0.55,
                                        0.50,
                                        app->personality.curiosity);
  lua_pushboolean(L, stored);
  return 1;
}

static int l_request_permission(lua_State* L) {
  const char* tool = luaL_checkstring(L, 1);
  const char* scope = luaL_optstring(L, 2, "*");
  moyu_app* app = get_app(L);
  char body[768];
  snprintf(body,
           sizeof(body),
           "Tool %s would like access to scope %s.",
           tool,
           scope);
  bool ok = state_add_inbox(
      app->state, "request", "MOYU is asking for permission", body);
  lua_pushboolean(L, ok);
  return 1;
}

// --- Appearance (skin) API -------------------------------------------------
// The pet's look is fully plugin-defined from Lua:
//   moyu.use_skin_builtin()                  -> reset to the procedural blob
//   moyu.use_skin_bmp(path, fw, fh)          -> load a BMP spritesheet
//   moyu.set_anim(name, frames, fps)         -> map an animation to sheet frames
// Both skins share the same fixed animation names: idle/blink/sleep/happy/sad/
// observe. A skin is just a sheet + a per-anim frame list; behaviour (when each
// anim plays) stays in on_tick/on_click.

static int l_use_skin_builtin(lua_State* L) {
  moyu_app* app = get_app(L);
  skin_free(&app->sk);
  skin_init_default(&app->sk);
  app->current_frame = 0;
  LOGI("[lua] skin: builtin");
  return 0;
}

static int l_use_skin_bmp(lua_State* L) {
  const char* path = luaL_checkstring(L, 1);
  int fw = (int)luaL_checkinteger(L, 2);
  int fh = (int)luaL_checkinteger(L, 3);
  moyu_app* app = get_app(L);
  // Resolve relative to the exe dir so "skins/cat.bmp" works regardless of cwd.
  char* full = NULL;
  if (path[0] == '/' || (path[0] && path[1] == ':')) {
    full = moyu_strdup(path);
  } else {
    const char* dir = platform_exe_dir();
    size_t a = strlen(dir), b = strlen(path);
    full = (char*)moyu_alloc(a + 1 + b + 1);
    memcpy(full, dir, a);
    full[a] = '\\';
    memcpy(full + a + 1, path, b + 1);
  }
  sprite_sheet_free(&app->sk.sheet);
  memset(&app->sk.sheet, 0, sizeof(app->sk.sheet));
  bool ok = skin_load_bmp(&app->sk, full, fw, fh);
  if (!ok) {
    // never render nothing — fall back to the builtin blob
    LOGW("[lua] skin BMP failed, falling back to builtin");
    skin_free(&app->sk);
    skin_init_default(&app->sk);
  }
  app->current_frame = 0;
  moyu_free(full);
  lua_pushboolean(L, ok);
  return 1;
}

static int l_set_anim(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  int fps = (int)luaL_optinteger(L, 3, 4);
  moyu_app* app = get_app(L);
  int id = anim_id_from_name(name);
  int frames[ANIM_MAX_FRAMES];
  int n = 0;
  int len = (int)lua_rawlen(L, 2);
  for (int i = 1; i <= len && n < ANIM_MAX_FRAMES; i++) {
    lua_rawgeti(L, 2, i);
    if (lua_isinteger(L, -1))
      frames[n++] = (int)lua_tointeger(L, -1);
    else if (lua_isnumber(L, -1))
      frames[n++] = (int)lua_tonumber(L, -1);
    lua_pop(L, 1);
  }
  skin_set_anim(&app->sk, id, frames, n, fps);
  app->current_frame = 0;
  return 0;
}

static void register_moyu_api(lua_State* L) {
  static const luaL_Reg funcs[] = {
      {"idle_seconds", l_idle_seconds},
      {"mouse_distance", l_mouse_distance},
      {"mood", l_mood},
      {"personality", l_personality},
      {"emit", l_emit},
      {"say", l_say},
      {"anim", l_anim},
      {"llm", l_llm},
      {"log", l_log},
      {"tool", l_tool},
      {"beliefs", l_beliefs},
      {"drives", l_beliefs},
      {"intention", l_intention},
      {"propose_memory", l_propose_memory},
      {"request_permission", l_request_permission},
      {"use_skin_builtin", l_use_skin_builtin},
      {"use_skin_bmp", l_use_skin_bmp},
      {"set_anim", l_set_anim},
      {NULL, NULL},
  };
  luaL_newlib(L, funcs);
  lua_setglobal(L, "moyu");
}

static void sandbox_libs(lua_State* L) {
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 1);
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  static const char* kill[] = {"dofile",
                               "loadfile",
                               "load",
                               "require",
                               "collectgarbage",
                               "getfenv",
                               "setfenv",
                               "rawequal",
                               "rawlen",
                               "rawget",
                               "rawset",
                               NULL};
  lua_pushglobaltable(L);
  for (int i = 0; kill[i]; i++) {
    lua_pushnil(L);
    lua_setfield(L, -2, kill[i]);
  }
  lua_pop(L, 1);
}

lua_State* lua_runtime_create(moyu_app* app, const char* script_path) {
  lua_State* L = luaL_newstate();
  if (!L) {
    LOGE("luaL_newstate failed");
    return NULL;
  }
  sandbox_libs(L);
  register_moyu_api(L);
  lua_pushstring(L, APP_KEY);
  lua_pushlightuserdata(L, (void*)app);
  lua_rawset(L, LUA_REGISTRYINDEX);

  if (script_path) {
    size_t flen = 0;
    char* src = platform_read_file(script_path, &flen);
    if (!src) {
      LOGW("Lua script not found: %s", script_path);
    } else {
      int rc = luaL_loadbuffer(L, src, flen, script_path);
      if (rc != LUA_OK) {
        LOGW("Lua load error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
      } else {
        rc = lua_pcall(L, 0, 0, 0);
        if (rc != LUA_OK) {
          LOGW("Lua run error: %s", lua_tostring(L, -1));
          lua_pop(L, 1);
        } else {
          LOGI("Loaded script: %s", script_path);
        }
      }
      moyu_free(src);
    }
  }
  return L;
}

void lua_runtime_destroy(lua_State* L) {
  if (L) lua_close(L);
}

void lua_runtime_call_on_tick(moyu_app* app,
                              float idle_s,
                              float mouse_dist,
                              float valence,
                              float arousal) {
  if (!app->L) return;
  lua_State* L = app->L;
  lua_getglobal(L, "on_tick");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_pushnumber(L, (lua_Number)idle_s);
  lua_pushnumber(L, (lua_Number)mouse_dist);
  lua_pushnumber(L, (lua_Number)valence);
  lua_pushnumber(L, (lua_Number)arousal);
  if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
    LOGW("on_tick: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lua_runtime_call_on_click(moyu_app* app, int button) {
  if (!app->L) return;
  lua_State* L = app->L;
  lua_getglobal(L, "on_click");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_pushinteger(L, button);
  if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    LOGW("on_click: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lua_runtime_call_on_event(moyu_app* app,
                               const char* event_name,
                               const char* payload_json) {
  if (!app || !app->L) return;
  lua_State* L = app->L;
  lua_getglobal(L, "on_event");
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return;
  }
  lua_pushstring(L, event_name ? event_name : "unknown");
  lua_pushstring(L, payload_json ? payload_json : "{}");
  if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
    LOGW("on_event: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

static void call_one_string_hook(moyu_app* app,
                                 const char* hook,
                                 const char* value) {
  if (!app || !app->L) return;
  lua_State* L = app->L;
  lua_getglobal(L, hook);
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
  lua_pushstring(L, value ? value : "{}");
  if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
    LOGW("%s: %s", hook, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
}

void lua_runtime_call_tool_result(moyu_app* app, const char* result_json) {
  call_one_string_hook(app, "on_tool_result", result_json);
}

void lua_runtime_call_memory_candidate(moyu_app* app, const char* summary) {
  call_one_string_hook(app, "on_memory_candidate", summary);
}

bool lua_runtime_propose_desire(moyu_app* app,
                                const char* state_json,
                                char* goal,
                                size_t goal_cap,
                                char* tool,
                                size_t tool_cap,
                                char* args_json,
                                size_t args_cap) {
  if (!app || !app->L) return false;
  lua_State* L = app->L;
  lua_getglobal(L, "propose_desires");
  if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
  lua_pushstring(L, state_json ? state_json : "{}");
  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    LOGW("propose_desires: %s", lua_tostring(L, -1)); lua_pop(L, 1); return false;
  }
  if (!lua_istable(L, -1)) { lua_pop(L, 1); return false; }
  lua_getfield(L, -1, "goal"); const char* g=lua_tostring(L,-1);
  lua_getfield(L, -2, "tool"); const char* t=lua_tostring(L,-1);
  lua_getfield(L, -3, "args"); const char* a=lua_tostring(L,-1);
  bool ok=g&&*g&&t&&*t;
  if(ok){snprintf(goal,goal_cap,"%s",g);snprintf(tool,tool_cap,"%s",t);snprintf(args_json,args_cap,"%s",a&&*a?a:"{}");}
  lua_pop(L,4);
  if(!ok)return false;
  lua_getglobal(L,"score_intention");
  if(lua_isfunction(L,-1)){lua_pushstring(L,goal);lua_pushstring(L,state_json?state_json:"{}");if(lua_pcall(L,2,1,0)==LUA_OK){double score=lua_isnumber(L,-1)?lua_tonumber(L,-1):1.0;lua_pop(L,1);return score>0;}LOGW("score_intention: %s",lua_tostring(L,-1));lua_pop(L,1);}else lua_pop(L,1);
  return true;
}
