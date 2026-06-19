#include "loop.h"
#include "../util/log.h"
#include "../util/mem.h"
#include "../render/bubble.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Forward-declared modules we use directly (no abstraction layer).
#include "../llm/completion.h"
#include "../llm/cache.h"
#include "../tools/tool.h"
#include "../script/lua_runtime.h"

// -------- helpers --------
static int distance_to_pet(moyu_app* app, int mx, int my) {
    // Pet occupies a 96x96 (3x of 32) area centered horizontally at the
    // bottom of the window.
    const int pet_size = 96;
    int cx = app->pet_x + (app->win_w - pet_size) / 2 + pet_size / 2;
    int cy = app->pet_y + (app->win_h - pet_size - 4) + pet_size / 2;
    int dx = mx - cx, dy = my - cy;
    return (int)sqrtf((float)(dx * dx + dy * dy));
}

static void track_llm_call(moyu_app* app) {
    uint64_t now = platform_now_ms();
    app->llm_calls[app->llm_calls_head] = now;
    app->llm_calls_head = (app->llm_calls_head + 1) % 128;
    if (app->llm_calls_count < 128) app->llm_calls_count++;
}

static int llm_calls_in_last_24h(moyu_app* app) {
    uint64_t now = platform_now_ms();
    uint64_t cutoff = now - 24ULL * 3600ULL * 1000ULL;
    int n = 0;
    for (size_t i = 0; i < app->llm_calls_count; i++) {
        size_t idx = (app->llm_calls_head + 128 - 1 - i) % 128;
        if (app->llm_calls[idx] >= cutoff) n++;
    }
    return n;
}

static uint64_t today_boundary_ms(void) {
    uint64_t now = platform_now_ms();
    return now - (now % (24ULL * 3600ULL * 1000ULL));
}

static int llm_calls_today(moyu_app* app) {
    uint64_t b = today_boundary_ms();
    int n = 0;
    for (size_t i = 0; i < app->llm_calls_count; i++) {
        size_t idx = (app->llm_calls_head + 128 - 1 - i) % 128;
        if (app->llm_calls[idx] >= b) n++;
    }
    return n;
}

void moyu_app_emit_say(moyu_app* app, const char* text, int duration_ms) {
    if (app->say_text) moyu_free(app->say_text);
    app->say_text = text ? moyu_strdup(text) : NULL;
    app->say_until_ms = platform_now_ms() + (uint64_t)duration_ms;
}

void moyu_app_emit_anim(moyu_app* app, int anim_id) {
    if (anim_id < 0 || anim_id >= ANIM_COUNT) return;
    if (app->current_anim == anim_id) return;
    app->current_anim = anim_id;
    app->current_frame = 0;
}

// Synchronous LLM call (blocks UI briefly). Lua calls this; we accept the latency.
bool moyu_app_request_llm(moyu_app* app, const char* prompt) {
    if (!app->llm_enabled) {
        LOGI("LLM disabled (no api_key); skipping request");
        return false;
    }
    if (llm_calls_today(app) >= app->llm_daily_limit) {
        LOGI("LLM daily limit reached (%d); skipping", app->llm_daily_limit);
        return false;
    }

    // Build messages: system prompt + recent context + user prompt
    char* ctx_json = context_to_json(&app->ctx, 8);
    char sys[512];
    snprintf(sys, sizeof(sys),
        "You are MOYU, a small pixel pet living on the user's desktop. "
        "You are not a productivity tool. Reply in one short sentence (<=40 chars ASCII), "
        "slightly sarcastic, like a living creature. "
        "Personality: sarcasm=%.2f curiosity=%.2f patience=%.2f mood=%.2f. "
        "Recent context (JSON): %s",
        app->personality.sarcasm, app->personality.curiosity,
        app->personality.patience, app->emotion.valence,
        ctx_json ? ctx_json : "[]");
    moyu_free(ctx_json);

    const char* msgs[2] = { sys, prompt };
    llm_result res = llm_complete(app->llm, NULL, msgs, 2, 15000);
    track_llm_call(app);
    if (res.error) {
        LOGW("LLM error: %s", res.error);
        llm_result_free(&res);
        return false;
    }
    if (res.text && res.text[0]) {
        // Push result into context + say bubble
        context_push(&app->ctx, CTX_REFLECTION, prompt, res.text, "{\"src\":\"llm\"}");
        moyu_app_emit_say(app, res.text, 5000);
    }
    llm_result_free(&res);
    return true;
}

// -------- per-iteration --------
static void handle_platform_event(moyu_app* app, const platform_event* pev) {
    switch (pev->type) {
        case PE_QUIT:
            app->running = false;
            break;
        case PE_MOUSE_MOVE:
            app->last_mouse_move_ms = pev->ts_ms;
            break;
        case PE_MOUSE_DOWN:
            // Click: switch to happy briefly
            moyu_app_emit_anim(app, ANIM_HAPPY);
            context_push(&app->ctx, CTX_INTERACTION, "click", "happy", NULL);
            break;
        default:
            break;
    }
}

static void handle_internal_event(moyu_app* app, const internal_event* ev) {
    switch (ev->kind) {
        case EV_LUA_ACTION: {
            if (!ev->str) break;
            if (strcmp(ev->str, "say") == 0 && ev->str2) {
                moyu_app_emit_say(app, ev->str2, 5000);
                context_push(&app->ctx, CTX_INTERACTION, "lua_say", ev->str2, NULL);
            } else if (strcmp(ev->str, "poke") == 0) {
                moyu_app_emit_anim(app, ANIM_HAPPY);
            } else if (strcmp(ev->str, "sleep") == 0) {
                moyu_app_emit_anim(app, ANIM_SLEEP);
            } else if (strcmp(ev->str, "llm") == 0 && ev->str2) {
                moyu_app_request_llm(app, ev->str2);
            } else if (strcmp(ev->str, "observe") == 0) {
                moyu_app_emit_anim(app, ANIM_OBSERVE);
            }
            break;
        }
        case EV_ANIM_REQUEST:
            moyu_app_emit_anim(app, ev->payload.i);
            break;
        case EV_SAY:
            if (ev->str) moyu_app_emit_say(app, ev->str, 5000);
            break;
        case EV_LLM_RESULT:
            if (ev->str) {
                context_push(&app->ctx, CTX_REFLECTION, "llm_async", ev->str, NULL);
                moyu_app_emit_say(app, ev->str, 5000);
            }
            break;
        case EV_TOOL_RESULT:
            context_push(&app->ctx, CTX_TOOL, ev->str ? ev->str : "tool",
                         ev->str2 ? ev->str2 : "", NULL);
            break;
        default:
            break;
    }
}

static void update_animation(moyu_app* app, uint64_t now_ms) {
    // Drive frame rate ~4 fps for sprite animation
    int frame_period = 250;  // ms per frame
    uint64_t elapsed = now_ms - app->last_tick_ms;
    (void)elapsed;
    int total_frames = app->sprites[app->current_anim].frame_count;
    if (total_frames <= 0) return;
    int frame = (int)((now_ms / frame_period) % total_frames);
    app->current_frame = frame;

    // Auto emotion-driven anim switches when nothing else is happening
    uint64_t idle = (now_ms - app->last_mouse_move_ms) / 1000;
    if (idle > 60 && app->current_anim == ANIM_IDLE) {
        // After 60s of no mouse activity, drift toward sleep
        if (app->emotion.arousal < -0.2f) moyu_app_emit_anim(app, ANIM_SLEEP);
    }
}

static void render_frame(moyu_app* app) {
    render_clear(&app->render, 0);  // transparent
    const int scale = 3;
    const int pet_size = 32 * scale;
    const int pet_dx = (app->win_w - pet_size) / 2;
    const int pet_dy = app->win_h - pet_size - 4;
    render_blit_frame_scaled(&app->render, &app->sprites[app->current_anim],
                             app->current_frame, pet_dx, pet_dy, scale);
    if (app->say_text && platform_now_ms() < app->say_until_ms) {
        render_bubble(&app->render, app->say_text, app->win_w / 2, pet_dy - 2);
    } else if (app->say_text) {
        moyu_free(app->say_text);
        app->say_text = NULL;
    }
    render_present(&app->render, app->win);
}

static void consider_lua_tick(moyu_app* app, uint64_t now_ms) {
    // Throttle Lua on_tick to ~1Hz
    if (app->last_tick_ms == 0) { app->last_tick_ms = now_ms; return; }
    if (now_ms - app->last_tick_ms < 1000) return;

    uint64_t idle_ms = now_ms - app->last_mouse_move_ms;
    float idle_s = (float)idle_ms / 1000.0f;
    app->last_tick_ms = now_ms;

    // Update mouse distance
    int mx, my;
    platform_get_cursor_pos(&mx, &my);
    int d = distance_to_pet(app, mx, my);
    int prev_d = app->last_mouse_distance;
    app->last_mouse_distance = d;
    if (d < 60) {
        if (!app->mouse_near) {
            app->mouse_near = true;
            moyu_app_emit_anim(app, ANIM_OBSERVE);
        }
    } else if (d > 120 && app->mouse_near) {
        app->mouse_near = false;
        moyu_app_emit_anim(app, ANIM_IDLE);
    }
    (void)prev_d;

    // Tick emotion
    emotion_tick(&app->emotion, &app->personality, now_ms, 1.0f);

    // Call Lua on_tick(idle_seconds, mouse_distance, mood_val, mood_aro)
    if (app->L) {
        lua_runtime_call_on_tick(app, idle_s, (float)d,
                                 app->emotion.valence, app->emotion.arousal);
    }

    // Idle-poke rule (rule-based, no LLM)
    if (idle_s > (float)app->idle_poke_seconds) {
        // Reset the mouse-move time so we don't fire every tick; add 50% jitter
        float r = (float)((now_ms % 1000) / 1000.0);
        if (r < 0.05f) {
            // 5% chance per second after threshold
            static const char* pokes[] = {
                "你又在摸鱼？", "...", "Zzz", "醒醒", "?", "嗯..."
            };
            int idx = (int)(r * 1000) % 6;
            (void)idx;
            int pick = (int)(now_ms % 6);
            moyu_app_emit_say(app, pokes[pick], 3000);
        }
    }

    // Rare-event: tiny chance of spontaneous reflection that triggers LLM
    if (app->llm_enabled && llm_calls_today(app) < app->llm_daily_limit) {
        float r = (float)((now_ms % 10000) / 10000.0);
        if (r < app->rare_event_chance * 0.001f) {
            // Ask LLM for a one-line reflection
            static const char* prompts[] = {
                "Say one short idle thought (<=30 chars ASCII).",
                "Tease the user briefly (<=30 chars ASCII).",
                "Make a tiny observation (<=30 chars ASCII)."
            };
            int pick = (int)(now_ms % 3);
            moyu_app_request_llm(app, prompts[pick]);
        }
    }
}

bool moyu_app_step(moyu_app* app) {
    if (!app->running) return false;
    platform_event pev;
    // 1Hz tick via 1000ms timeout; events come back sooner if there's input
    bool got = platform_poll_event(app->win, &pev, 1000);
    if (got) handle_platform_event(app, &pev);

    // Drain internal events
    internal_event ev;
    while (event_queue_pop(&app->events, &ev)) {
        handle_internal_event(app, &ev);
        if (ev.str)  moyu_free(ev.str);
        if (ev.str2) moyu_free(ev.str2);
    }

    uint64_t now = platform_now_ms();
    consider_lua_tick(app, now);
    update_animation(app, now);
    render_frame(app);

    return app->running;
}

int moyu_app_run(moyu_app* app) {
    app->running = true;
    app->last_mouse_move_ms = platform_now_ms();
    app->last_tick_ms = 0;
    platform_window_show(app->win);
    while (moyu_app_step(app)) {
        // single iteration; sleep happens via platform_poll_event timeout
    }
    return 0;
}
