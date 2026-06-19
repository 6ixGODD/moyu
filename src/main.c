#include "core/loop.h"
#include "core/context.h"
#include "core/personality.h"
#include "core/emotion.h"
#include "core/event.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/sprite.h"
#include "render/procedural.h"
#include "llm/completion.h"
#include "llm/cache.h"
#include "tools/tool.h"
#include "tools/mcp.h"
#include "script/lua_runtime.h"
#include "util/log.h"
#include "util/mem.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define WIN_W 160
#define WIN_H 160
#define PET_SCALE 3
#define PET_SIZE  (32 * PET_SCALE)   // 96

static char* path_relative_to_exe(const char* name) {
    const char* dir = platform_exe_dir();
    return platform_join_path(dir, name);
}

static bool load_config(const char* path, moyu_app* app) {
    size_t len = 0;
    char* text = platform_read_file(path, &len);
    if (!text) {
        LOGW("config not found: %s (using defaults)", path);
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(text, len);
    moyu_free(text);
    if (!root) { LOGW("config parse error"); return false; }

    cJSON* llm = cJSON_GetObjectItem(root, "llm");
    if (llm) {
        cJSON* v;
        v = cJSON_GetObjectItem(llm, "base_url");
        if (v && cJSON_IsString(v)) { moyu_free(app->llm->base_url); app->llm->base_url = moyu_strdup(v->valuestring); }
        v = cJSON_GetObjectItem(llm, "api_key");
        if (v && cJSON_IsString(v)) { moyu_free(app->llm->api_key); app->llm->api_key = moyu_strdup(v->valuestring); }
        v = cJSON_GetObjectItem(llm, "model");
        if (v && cJSON_IsString(v)) { moyu_free(app->llm->model); app->llm->model = moyu_strdup(v->valuestring); }
        v = cJSON_GetObjectItem(llm, "max_tokens");
        if (v && cJSON_IsNumber(v)) app->llm->max_tokens = v->valueint;
        v = cJSON_GetObjectItem(llm, "temperature");
        if (v && cJSON_IsNumber(v)) app->llm->temperature = (float)v->valuedouble;
        v = cJSON_GetObjectItem(llm, "json_mode");
        if (v && cJSON_IsBool(v)) app->llm->json_mode = cJSON_IsTrue(v);
        v = cJSON_GetObjectItem(llm, "daily_limit");
        if (v && cJSON_IsNumber(v)) app->llm_daily_limit = v->valueint;
    }
    cJSON* p = cJSON_GetObjectItem(root, "personality");
    if (p) {
        cJSON* v;
        v = cJSON_GetObjectItem(p, "mood_bias");  if (v && cJSON_IsNumber(v)) app->personality.mood_bias = (float)v->valuedouble;
        v = cJSON_GetObjectItem(p, "sarcasm");    if (v && cJSON_IsNumber(v)) app->personality.sarcasm   = (float)v->valuedouble;
        v = cJSON_GetObjectItem(p, "curiosity");  if (v && cJSON_IsNumber(v)) app->personality.curiosity = (float)v->valuedouble;
        v = cJSON_GetObjectItem(p, "patience");   if (v && cJSON_IsNumber(v)) app->personality.patience  = (float)v->valuedouble;
    }
    cJSON* rules = cJSON_GetObjectItem(root, "rules");
    if (rules) {
        cJSON* v;
        v = cJSON_GetObjectItem(rules, "idle_poke_seconds");
        if (v && cJSON_IsNumber(v)) app->idle_poke_seconds = v->valueint;
        v = cJSON_GetObjectItem(rules, "rare_event_chance");
        if (v && cJSON_IsNumber(v)) app->rare_event_chance = (float)v->valuedouble;
    }

    // MCP servers (dynamic tool loading)
    cJSON* servers = cJSON_GetObjectItem(root, "mcp_servers");
    if (servers && cJSON_IsArray(servers)) {
        int n = cJSON_GetArraySize(servers);
        for (int i = 0; i < n; i++) {
            cJSON* s = cJSON_GetArrayItem(servers, i);
            cJSON* url = cJSON_GetObjectItem(s, "url");
            cJSON* key = cJSON_GetObjectItem(s, "api_key");
            if (!url || !cJSON_IsString(url)) continue;
            mcp_client* mc = (mcp_client*)moyu_alloc(sizeof(mcp_client));
            mcp_client_init(mc, url->valuestring, key && cJSON_IsString(key) ? key->valuestring : NULL);
            LOGI("Loading MCP server: %s", url->valuestring);
            if (mcp_register_tools(mc, app->tools)) {
                LOGI("MCP tools registered (%zu)", mc->tool_count);
            } else {
                LOGW("MCP connect failed: %s", url->valuestring);
            }
            // Leak mc for program lifetime (small); could track and free later.
        }
    }

    cJSON_Delete(root);
    return true;
}

// Built-in tools ----------------------------------------------------------
static char* tool_say(const char* input_json, void* user) {
    moyu_app* app = (moyu_app*)user;
    cJSON* root = cJSON_Parse(input_json);
    if (!root) return moyu_strdup("{\"error\":\"bad json\"}");
    cJSON* text = cJSON_GetObjectItem(root, "text");
    const char* t = (text && cJSON_IsString(text)) ? text->valuestring : "";
    moyu_app_emit_say(app, t, 5000);
    cJSON_Delete(root);
    return moyu_strdup("{\"ok\":true}");
}

static char* tool_poke(const char* input_json, void* user) {
    (void)input_json;
    moyu_app* app = (moyu_app*)user;
    moyu_app_emit_anim(app, ANIM_HAPPY);
    return moyu_strdup("{\"ok\":true}");
}

static char* tool_distance(const char* input_json, void* user) {
    (void)input_json;
    moyu_app* app = (moyu_app*)user;
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"distance\":%d}", app->last_mouse_distance);
    return moyu_strdup(buf);
}

static void register_builtin_tools(moyu_app* app) {
    tool_def say = { "say", "Make the pet say something.",
                     "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}}}",
                     tool_say, app };
    tool_def poke = { "poke", "Poke the pet (happy reaction).",
                      "{\"type\":\"object\"}", tool_poke, app };
    tool_def distance = { "mouse_distance", "Get distance from cursor to pet (pixels).",
                          "{\"type\":\"object\"}", tool_distance, app };
    tool_registry_add(app->tools, say);
    tool_registry_add(app->tools, poke);
    tool_registry_add(app->tools, distance);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR cmd, int show)
#else
int main(int argc, char** argv)
#endif
{
#ifdef _WIN32
    (void)hI; (void)hP; (void)cmd; (void)show;
    // Attach a console for log output if launched from one.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stderr);
    }
#else
    (void)argc; (void)argv;
#endif

    log_set_level(LOG_INFO);
    // Also mirror logs to a file next to the exe for debugging GUI runs.
    {
        char* log_path = path_relative_to_exe("moyu.log");
        log_set_file(log_path);
        moyu_free(log_path);
    }

    moyu_app app;
    memset(&app, 0, sizeof(app));
    app.win_w = WIN_W;
    app.win_h = WIN_H;
    app.idle_poke_seconds = 300;
    app.rare_event_chance = 0.02f;
    app.llm_daily_limit = 50;

    // LLM + cache
    llm_config llm_cfg;
    llm_config_init(&llm_cfg);
    app.llm = &llm_cfg;

    llm_cache cache;
    llm_cache_init(&cache, 64);
    app.cache = &cache;

    // Tools
    tool_registry tools;
    tool_registry_init(&tools, 32);
    app.tools = &tools;
    register_builtin_tools(&app);

    // Core state
    context_store_init(&app.ctx, 64);
    personality_defaults(&app.personality);
    emotion_init(&app.emotion, &app.personality);
    event_queue_init(&app.events, 64);

    // Render
    app.sprites = procedural_build_all();
    app.render = render_new(WIN_W, WIN_H);
    app.current_anim = ANIM_IDLE;
    app.current_frame = 0;

    // Config
    char* cfg_path = path_relative_to_exe("assets\\config.json");
    load_config(cfg_path, &app);
    moyu_free(cfg_path);

    // Determine LLM enabled state
    app.llm_enabled = app.llm->api_key && app.llm->api_key[0];
    if (!app.llm_enabled) LOGI("LLM disabled (empty api_key in config.json)");

    // Position pet near bottom-right of primary screen
    int sx = 0, sy = 0, sw = 1920, sh = 1080;
#ifdef _WIN32
    {
        RECT rc;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
        sx = rc.left; sy = rc.top; sw = rc.right - rc.left; sh = rc.bottom - rc.top;
    }
#endif
    app.pet_x = sx + sw - WIN_W - 40;
    app.pet_y = sy + sh - WIN_H - 40;

    // Window
    app.win = platform_window_create(WIN_W, WIN_H, app.pet_x, app.pet_y,
                                      /*transparent=*/true, /*topmost=*/true,
                                      /*click_through=*/false);
    if (!app.win) {
        LOGE("window create failed");
        return 1;
    }
    // Only the pet area (bottom-centered 96x96) captures mouse; the rest of
    // the transparent window (including the bubble region above) passes
    // clicks through to the desktop.
    {
        int pet_dx = (WIN_W - PET_SIZE) / 2;
        int pet_dy = WIN_H - PET_SIZE - 4;
        platform_window_set_clickable(app.win, pet_dx, pet_dy, PET_SIZE, PET_SIZE);
    }

    // Lua script
    char* script_path = path_relative_to_exe("scripts\\default.lua");
    app.L = lua_runtime_create(&app, script_path);
    moyu_free(script_path);

    // Initial context node
    context_push(&app.ctx, CTX_IDLE, "boot", "ok", "{\"ver\":\"0.1\"}");

    LOGI("MOYU starting (window %dx%d at %d,%d)", WIN_W, WIN_H, app.pet_x, app.pet_y);

    int rc = moyu_app_run(&app);

    // Cleanup
    if (app.say_text) moyu_free(app.say_text);
    if (app.L) lua_runtime_destroy(app.L);
    platform_window_destroy(app.win);
    for (int i = 0; i < ANIM_COUNT; i++) sprite_sheet_free(&app.sprites[i]);
    moyu_free(app.sprites);
    render_free(&app.render);
    event_queue_free(&app.events);
    context_store_free(&app.ctx);
    tool_registry_free(&tools);
    llm_cache_free(&cache);
    llm_config_free(&llm_cfg);
    return rc;
}
