#pragma once
#include "context.h"
#include "personality.h"
#include "emotion.h"
#include "event.h"
#include "../render/render.h"
#include "../render/sprite.h"
#include "../platform/platform.h"

#include <stdint.h>
#include <stdbool.h>

// Forward declarations to avoid include cycles.
struct lua_State;
struct llm_config;
struct llm_cache;
struct tool_registry;

typedef struct moyu_app {
    // platform
    platform_window* win;
    int win_w, win_h;
    int pet_x, pet_y;        // screen coords of pet's top-left

    // render
    render_ctx render;
    sprite_sheet* sprites;   // ANIM_COUNT sheets (owned array)

    // core state
    context_store  ctx;
    personality    personality;
    emotion        emotion;
    event_queue    events;

    // script
    struct lua_State* L;

    // llm
    struct llm_config* llm;
    struct llm_cache*  cache;

    // tools
    struct tool_registry* tools;

    // runtime
    int      current_anim;
    int      current_frame;
    int      frame_counter;       // increments every iteration; modulo used for frame rate
    uint64_t last_tick_ms;
    uint64_t last_mouse_move_ms;
    int      last_mouse_distance;
    bool     mouse_near;

    // say bubble
    char*    say_text;            // owned
    uint64_t say_until_ms;

    // LLM throttle: ring of recent call timestamps (ms)
    uint64_t llm_calls[128];
    size_t   llm_calls_count, llm_calls_head;

    // config: behaviour thresholds
    int      idle_poke_seconds;
    float    rare_event_chance;
    int      llm_daily_limit;
    bool     llm_enabled;         // false if api_key empty

    bool     running;
} moyu_app;

// One iteration of the main loop. Returns false on EV_QUIT.
bool moyu_app_step(moyu_app* app);

// Block-running wrapper.
int  moyu_app_run(moyu_app* app);

// Helpers used by Lua bindings:
void moyu_app_emit_say(moyu_app* app, const char* text, int duration_ms);
void moyu_app_emit_anim(moyu_app* app, int anim_id);
bool moyu_app_request_llm(moyu_app* app, const char* prompt);  // synchronous from Lua's POV
