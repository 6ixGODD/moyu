#include "lua_runtime.h"
#include "../util/log.h"
#include "../util/mem.h"
#include "../platform/platform.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

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
    uint64_t idle = (now > app->last_mouse_move_ms) ? (now - app->last_mouse_move_ms) : 0;
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
    int id = ANIM_IDLE;
    if      (strcmp(name, "idle") == 0)     id = ANIM_IDLE;
    else if (strcmp(name, "blink") == 0)    id = ANIM_BLINK;
    else if (strcmp(name, "sleep") == 0)    id = ANIM_SLEEP;
    else if (strcmp(name, "happy") == 0)    id = ANIM_HAPPY;
    else if (strcmp(name, "sad") == 0)      id = ANIM_SAD;
    else if (strcmp(name, "observe") == 0)  id = ANIM_OBSERVE;
    moyu_app_emit_anim(app, id);
    return 0;
}

static int l_llm(lua_State* L) {
    const char* prompt = luaL_checkstring(L, 1);
    moyu_app* app = get_app(L);
    bool ok = moyu_app_request_llm(app, prompt);
    if (!ok) { lua_pushnil(L); return 1; }
    if (app->say_text) lua_pushstring(L, app->say_text);
    else lua_pushnil(L);
    return 1;
}

static int l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    LOGI("[lua] %s", msg);
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
        {NULL, NULL},
    };
    luaL_newlib(L, funcs);
    lua_setglobal(L, "moyu");
}

static void sandbox_libs(lua_State* L) {
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    static const char* kill[] = {
        "dofile", "loadfile", "load", "require", "collectgarbage",
        "getfenv", "setfenv", "rawequal", "rawlen", "rawget", "rawset",
        NULL
    };
    lua_pushglobaltable(L);
    for (int i = 0; kill[i]; i++) {
        lua_pushnil(L);
        lua_setfield(L, -2, kill[i]);
    }
    lua_pop(L, 1);
}

lua_State* lua_runtime_create(moyu_app* app, const char* script_path) {
    lua_State* L = luaL_newstate();
    if (!L) { LOGE("luaL_newstate failed"); return NULL; }
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

void lua_runtime_call_on_tick(moyu_app* app, float idle_s, float mouse_dist,
                              float valence, float arousal) {
    if (!app->L) return;
    lua_State* L = app->L;
    lua_getglobal(L, "on_tick");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
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
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushinteger(L, button);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        LOGW("on_click: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}
