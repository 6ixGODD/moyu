#include "cJSON.h"
#include "agent.h"
#include "async.h"
#include "chat.h"
#include "builtin.h"
#include "context.h"
#include "emotion.h"
#include "event.h"
#include "image.h"
#include "llm.h"
#include "log.h"
#include "loop.h"
#include "lua_rt.h"
#include "mcp.h"
#include "memory.h"
#include "mem.h"
#include "persona.h"
#include "platform.h"
#include "procedural.h"
#include "render.h"
#include "secrets.h"
#include "sprite.h"
#include "state.h"
#include "tool.h"
#include "workdir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define WIN_W 220
#define WIN_H 220
#define PET_FRAME_SIZE 48
#define PET_SIZE (PET_FRAME_SIZE * PET_SCALE)

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
  if (!root) {
    LOGW("config parse error");
    return false;
  }

  cJSON* llm = cJSON_GetObjectItem(root, "llm");
  if (llm) {
    cJSON* v;
    v = cJSON_GetObjectItem(llm, "base_url");
    if (v && cJSON_IsString(v)) {
      moyu_free(app->llm->base_url);
      app->llm->base_url = moyu_strdup(v->valuestring);
    }
    v = cJSON_GetObjectItem(llm, "api_key");
    if (v && cJSON_IsString(v)) {
      moyu_free(app->llm->api_key);
      app->llm->api_key = moyu_strdup(v->valuestring);
    }
    v = cJSON_GetObjectItem(llm, "model");
    if (v && cJSON_IsString(v)) {
      moyu_free(app->llm->model);
      app->llm->model = moyu_strdup(v->valuestring);
    }
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
    v = cJSON_GetObjectItem(p, "mood_bias");
    if (v && cJSON_IsNumber(v))
      app->personality.mood_bias = (float)v->valuedouble;
    v = cJSON_GetObjectItem(p, "sarcasm");
    if (v && cJSON_IsNumber(v))
      app->personality.sarcasm = (float)v->valuedouble;
    v = cJSON_GetObjectItem(p, "curiosity");
    if (v && cJSON_IsNumber(v))
      app->personality.curiosity = (float)v->valuedouble;
    v = cJSON_GetObjectItem(p, "patience");
    if (v && cJSON_IsNumber(v))
      app->personality.patience = (float)v->valuedouble;
  }
  cJSON* rules = cJSON_GetObjectItem(root, "rules");
  if (rules) {
    cJSON* v;
    v = cJSON_GetObjectItem(rules, "idle_poke_seconds");
    if (v && cJSON_IsNumber(v)) app->idle_poke_seconds = v->valueint;
    v = cJSON_GetObjectItem(rules, "rare_event_chance");
    if (v && cJSON_IsNumber(v)) app->rare_event_chance = (float)v->valuedouble;
  }

  cJSON* privacy = cJSON_GetObjectItem(root, "privacy");
  cJSON* roots = privacy ? cJSON_GetObjectItem(privacy, "observe_roots") : NULL;
  if (roots && cJSON_IsArray(roots) && cJSON_GetArraySize(roots) > 0) {
    cJSON* first = cJSON_GetArrayItem(roots, 0);
    if (cJSON_IsString(first) && first->valuestring[0]) {
      if (app->observe_root) moyu_free(app->observe_root);
      app->observe_root = moyu_strdup(first->valuestring);
      if (app->state)
        state_permission_set(app->state,
                             "filesystem.observe",
                             app->observe_root,
                             "allow",
                             true);
    }
  }
  cJSON* autonomy = cJSON_GetObjectItem(root, "autonomy");
  if (autonomy && app->agent) {
    cJSON* enabled = cJSON_GetObjectItem(autonomy, "enabled");
    if (enabled && cJSON_IsBool(enabled))
      app->agent->autonomous_enabled = cJSON_IsTrue(enabled);
  }

  // MCP servers (dynamic tool loading)
  cJSON* servers = cJSON_GetObjectItem(root, "mcp_servers");
  if (servers && cJSON_IsArray(servers)) {
    int n = cJSON_GetArraySize(servers);
    for (int i = 0; i < n; i++) {
      cJSON* s = cJSON_GetArrayItem(servers, i);
      cJSON* name = cJSON_GetObjectItem(s, "name");
      cJSON* transport = cJSON_GetObjectItem(s, "transport");
      cJSON* url = cJSON_GetObjectItem(s, "url");
      cJSON* key = cJSON_GetObjectItem(s, "api_key");
      mcp_client* mc = (mcp_client*)moyu_alloc(sizeof(mcp_client));
      const char* server_name = name && cJSON_IsString(name)
                                    ? name->valuestring
                                    : "mcp";
      bool is_stdio = transport && cJSON_IsString(transport) &&
                      strcmp(transport->valuestring, "stdio") == 0;
      if (is_stdio) {
        cJSON* command = cJSON_GetObjectItem(s, "command");
        cJSON* args = cJSON_GetObjectItem(s, "args");
        cJSON* cwd = cJSON_GetObjectItem(s, "cwd");
        if (!command || !cJSON_IsString(command)) {
          moyu_free(mc);
          continue;
        }
        char cmdline[4096];
        snprintf(cmdline, sizeof(cmdline), "\"%s\"", command->valuestring);
        if (args && cJSON_IsArray(args)) {
          int ac = cJSON_GetArraySize(args);
          for (int ai = 0; ai < ac; ai++) {
            cJSON* av = cJSON_GetArrayItem(args, ai);
            if (!cJSON_IsString(av)) continue;
            strncat(cmdline, " \"", sizeof(cmdline) - strlen(cmdline) - 1);
            strncat(cmdline,
                    av->valuestring,
                    sizeof(cmdline) - strlen(cmdline) - 1);
            strncat(cmdline, "\"", sizeof(cmdline) - strlen(cmdline) - 1);
          }
        }
        mcp_client_init_stdio(
            mc,
            server_name,
            cmdline,
            cwd && cJSON_IsString(cwd) ? cwd->valuestring : NULL);
      } else {
        if (!url || !cJSON_IsString(url)) {
          moyu_free(mc);
          continue;
        }
        mcp_client_init_http(
            mc,
            server_name,
            url->valuestring,
            key && cJSON_IsString(key) ? key->valuestring : NULL);
      }
      LOGI("Loading MCP server: %s", server_name);
      if (mcp_register_tools(mc, app->tools)) {
        LOGI("MCP tools registered (%zu)", mc->tool_count);
      } else {
        LOGW("MCP connect failed: %s", server_name);
      }
      if (app->mcp_client_count == app->mcp_client_cap) {
        app->mcp_client_cap = app->mcp_client_cap ? app->mcp_client_cap * 2 : 4;
        app->mcp_clients = (mcp_client**)moyu_realloc(
            app->mcp_clients, app->mcp_client_cap * sizeof(mcp_client*));
      }
      app->mcp_clients[app->mcp_client_count++] = mc;
    }
  }

  cJSON_Delete(root);
  return true;
}

static bool write_user_config(const char* path, const moyu_app* app) {
  cJSON* root = cJSON_CreateObject();
  cJSON* llm = cJSON_AddObjectToObject(root, "llm");
  cJSON_AddStringToObject(llm, "base_url", app->llm->base_url);
  cJSON_AddStringToObject(llm, "api_key_source", "windows_dpapi");
  cJSON_AddStringToObject(llm, "model", app->llm->model);
  cJSON_AddNumberToObject(llm, "max_tokens", app->llm->max_tokens);
  cJSON_AddNumberToObject(llm, "temperature", app->llm->temperature);
  cJSON_AddNumberToObject(llm, "daily_limit", app->llm_daily_limit);
  cJSON_AddItemToObject(root, "mcp_servers", cJSON_CreateArray());
  cJSON* privacy = cJSON_AddObjectToObject(root, "privacy");
  cJSON_AddItemToObject(privacy, "observe_roots", cJSON_CreateArray());
  cJSON* autonomy = cJSON_AddObjectToObject(root, "autonomy");
  cJSON_AddBoolToObject(autonomy, "enabled", 1);
  cJSON_AddBoolToObject(autonomy, "silent_only", 0);
  char* text = cJSON_Print(root);
  cJSON_Delete(root);
  bool ok = text && platform_write_file_atomic(path, text, strlen(text));
  if (text) moyu_free(text);
  return ok;
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
  tool_def say = {
      .name = "pet.say",
      .description = "Make the pet say something.",
      .input_schema_json = "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\"}}}",
      .source = "builtin",
      .affordance = "{\"domain\":\"pet\",\"actions\":[\"express\"]}",
      .risk = TOOL_OBSERVE,
      .invoke = tool_say,
      .user = app};
  tool_def poke = {.name = "pet.poke",
                   .description = "Poke the pet.",
                   .input_schema_json = "{\"type\":\"object\"}",
                   .source = "builtin",
                   .affordance = "{\"domain\":\"pet\",\"actions\":[\"express\"]}",
                   .risk = TOOL_OBSERVE,
                   .invoke = tool_poke,
                   .user = app};
  tool_def distance = {.name = "system.mouse_distance",
                       .description = "Get distance from cursor to pet in pixels.",
                       .input_schema_json = "{\"type\":\"object\"}",
                       .source = "builtin",
                       .affordance = "{\"domain\":\"system\",\"senses\":[\"proximity\"]}",
                       .risk = TOOL_OBSERVE,
                       .invoke = tool_distance,
                       .user = app};
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
  bool smoke_llm = false;
#ifdef _WIN32
  (void)hI;
  (void)hP;
  smoke_llm = cmd && strstr(cmd, "--smoke-llm") != NULL;
  (void)show;
  // Attach a console for log output if launched from one.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    freopen("CONOUT$", "w", stderr);
  }
#else
  (void)argc;
  (void)argv;
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

  // Persistent home and state. The old executable-relative configuration is
  // still accepted below for a seamless v0.1 migration.
  moyu_workdir workdir;
  state_store state;
  memory_system memory;
  memset(&workdir, 0, sizeof(workdir));
  memset(&state, 0, sizeof(state));
  memset(&memory, 0, sizeof(memory));
  if (!workdir_init(&workdir) || !state_open(&state, workdir.db_path) ||
      !memory_init(&memory, &workdir, &state)) {
    LOGE("persistent runtime initialization failed");
    workdir_free(&workdir);
    return 1;
  }
  app.workdir = &workdir;
  app.state = &state;
  app.memory = &memory;
  builtin_register_persistent(&app);

  agent_runtime agent;
  agent_init(&agent, &app);
  app.agent = &agent;

  // Render
  skin_init_default(&app.sk);
  app.render = render_new(WIN_W, WIN_H);
  app.current_anim = ANIM_IDLE;
  app.current_frame = 0;
  app.render_dirty = true;
  app.anim_until_ms = platform_now_ms() + 2200;

  // Config
  bool had_user_config = platform_file_exists(workdir.config_path);
  if (had_user_config) {
    load_config(workdir.config_path, &app);
  } else {
    char* cfg_path = path_relative_to_exe("assets\\config.json");
    load_config(cfg_path, &app);
    moyu_free(cfg_path);
  }
  if (app.llm->api_key && app.llm->api_key[0]) {
    secrets_store(workdir.secrets_path, app.llm->api_key);
    if (!had_user_config) write_user_config(workdir.config_path, &app);
  } else {
    char* secret = secrets_load(workdir.secrets_path);
    if (secret && secret[0]) {
      moyu_free(app.llm->api_key);
      app.llm->api_key = secret;
    } else if (secret) {
      moyu_free(secret);
    }
  }
  if (!app.observe_root) {
    app.observe_root = state_meta_get(&state, "observe_root");
    if (app.observe_root)
      state_permission_set(&state,
                           "filesystem.observe",
                           app.observe_root,
                           "allow",
                           true);
  }
  {
    char* autonomy = state_meta_get(&state, "autonomy_enabled");
    if (autonomy) {
      app.agent->autonomous_enabled = strcmp(autonomy, "0") != 0;
      moyu_free(autonomy);
    }
  }
  app.last_collection_title = state_meta_get(&state, "last_collection_title");
  app.last_collection_body = state_meta_get(&state, "last_collection_body");

  // Determine LLM enabled state
  app.llm_enabled = app.llm->api_key && app.llm->api_key[0];
  if (!app.llm_enabled) LOGI("LLM disabled (empty api_key in config.json)");

  if (smoke_llm) {
    const char* messages[2] = {
        "You are MOYU. Reply with exactly: MOYU_SMOKE_OK",
        "Runtime connectivity smoke test."};
    llm_result smoke = llm_complete(app.llm, NULL, messages, 2, 30000);
    bool ok = smoke.text && strstr(smoke.text, "MOYU_SMOKE_OK") != NULL;
    LOGI("LLM smoke: %s", ok ? "OK" : "FAILED");
    if (smoke.error) LOGE("LLM smoke error: %s", smoke.error);
    llm_result_free(&smoke);
    skin_free(&app.sk);
    render_free(&app.render);
    memory_free(&memory);
    state_close(&state);
    workdir_free(&workdir);
    event_queue_free(&app.events);
    context_store_free(&app.ctx);
    tool_registry_free(&tools);
    llm_cache_free(&cache);
    llm_config_free(&llm_cfg);
    return ok ? 0 : 2;
  }

  // Position pet near bottom-right of primary screen
  int sx = 0, sy = 0, sw = 1920, sh = 1080;
#ifdef _WIN32
  {
    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    sx = rc.left;
    sy = rc.top;
    sw = rc.right - rc.left;
    sh = rc.bottom - rc.top;
  }
#endif
  app.pet_x = sx + sw - WIN_W - 40;
  app.pet_y = sy + sh - WIN_H - 40;

  {
    int pet_dx = (WIN_W - PET_SIZE) / 2;
    int pet_dy = WIN_H - PET_SIZE - 4;
    LOGI("screen workarea: %d,%d %dx%d", sx, sy, sw, sh);
    LOGI("window at %d,%d (%dx%d), pet sub-rect %d,%d %dx%d (scale=%d)",
         app.pet_x,
         app.pet_y,
         WIN_W,
         WIN_H,
         pet_dx,
         pet_dy,
         PET_SIZE,
         PET_SIZE,
         PET_SCALE);
  }

  // Window
  app.win = platform_window_create(WIN_W,
                                   WIN_H,
                                   app.pet_x,
                                   app.pet_y,
                                   /*transparent=*/true,
                                   /*topmost=*/true,
                                   /*click_through=*/false);
  if (!app.win) {
    LOGE("window create failed");
    return 1;
  }
  app.async = async_worker_create(app.llm, app.win);
  if (!app.async) {
    LOGE("I/O worker create failed");
    platform_window_destroy(app.win);
    return 1;
  }
  app.chat = chat_ui_create(&app);
  if (!app.chat) {
    LOGE("chat UI create failed");
    async_worker_destroy(app.async);
    platform_window_destroy(app.win);
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
  state_add_episode(&state,
                    "lifecycle",
                    "MOYU woke up and recovered its home.",
                    "{\"event\":\"boot\"}",
                    0.15,
                    0.05);
  state_cleanup(&state, 90);

  LOGI("MOYU starting (window %dx%d at %d,%d)",
       WIN_W,
       WIN_H,
       app.pet_x,
       app.pet_y);

  if (!getenv("MOYU_SKIP_ONBOARDING")) chat_ui_onboarding(app.chat);

  int rc = moyu_app_run(&app);

  // Cleanup
  if (app.say_text) moyu_free(app.say_text);
  if (app.info_title) moyu_free(app.info_title);
  if (app.info_body) moyu_free(app.info_body);
  if (app.last_collection_title) moyu_free(app.last_collection_title);
  if (app.last_collection_body) moyu_free(app.last_collection_body);
  if (app.observe_root) moyu_free(app.observe_root);
  if (app.L) lua_runtime_destroy(app.L);
  chat_ui_destroy(app.chat);
  async_worker_destroy(app.async);
  platform_window_destroy(app.win);
  skin_free(&app.sk);
  render_free(&app.render);
  event_queue_free(&app.events);
  context_store_free(&app.ctx);
  for (size_t i = 0; i < app.mcp_client_count; i++) {
    mcp_client_free(app.mcp_clients[i]);
    moyu_free(app.mcp_clients[i]);
  }
  if (app.mcp_clients) moyu_free(app.mcp_clients);
  memory_free(&memory);
  state_close(&state);
  workdir_free(&workdir);
  tool_registry_free(&tools);
  llm_cache_free(&cache);
  llm_config_free(&llm_cfg);
  return rc;
}
