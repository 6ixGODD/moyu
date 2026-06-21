#include "loop.h"

#include "font.h"
#include "agent.h"
#include "async.h"
#include "builtin.h"
#include "chat.h"
#include "log.h"
#include "mem.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward-declared modules we use directly (no abstraction layer).
#include "llm.h"
#include "lua_rt.h"
#include "memory.h"
#include "state.h"
#include "tool.h"

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

static void replace_owned(char** dst, const char* src) {
  if (*dst) moyu_free(*dst);
  *dst = src ? moyu_strdup(src) : NULL;
}

static const char* mood_label(const moyu_app* app) {
  if (app->pet_dragging) return "being carried";
  if (app->emotion.arousal < -0.25f) return "sleepy";
  if (app->emotion.valence > 0.35f) return "warm";
  if (app->mouse_near || app->current_anim == ANIM_OBSERVE) return "curious";
  return "quiet";
}

void moyu_app_emit_say(moyu_app* app, const char* text, int duration_ms) {
  if (app->say_text) moyu_free(app->say_text);
  app->say_text = text ? moyu_strdup(text) : NULL;
  app->say_until_ms = platform_now_ms() + (uint64_t)duration_ms;
  app->render_dirty = true;
}

void moyu_app_emit_info(moyu_app* app,
                        const char* title,
                        const char* body,
                        int duration_ms) {
  replace_owned(&app->info_title, title);
  replace_owned(&app->info_body, body);
  app->info_until_ms = platform_now_ms() + (uint64_t)duration_ms;
  app->render_dirty = true;
}

void moyu_app_note_collection(moyu_app* app,
                              const char* title,
                              const char* body) {
  replace_owned(&app->last_collection_title, title);
  replace_owned(&app->last_collection_body, body);
  moyu_app_emit_info(app, "Collection updated", title, 5200);
  moyu_app_emit_say(app, "我把它收好了。", 3600);
  moyu_app_emit_anim(app, ANIM_FOUND);
  emotion_react(&app->emotion, 0.14f, 0.16f);
}

void moyu_app_emit_anim(moyu_app* app, int anim_id) {
  if (anim_id < 0 || anim_id >= ANIM_COUNT) return;
  if (app->current_anim == anim_id) return;
  app->current_anim = anim_id;
  app->current_frame = 0;
  app->anim_until_ms = platform_now_ms() + 2200;
  app->render_dirty = true;
}

bool moyu_app_request_llm_for(moyu_app* app,
                              const char* prompt,
                              const char* purpose) {
  if (!app->llm_enabled) {
    LOGI("LLM disabled (no api_key); skipping request");
    return false;
  }
  if (!app->async || async_busy(app->async)) {
    LOGI("LLM worker busy; request deferred");
    return false;
  }
  if (llm_calls_today(app) >= app->llm_daily_limit ||
      (app->state &&
       !state_budget_take(app->state, "llm_total", app->llm_daily_limit, 1))) {
    LOGI("LLM daily limit reached (%d); skipping", app->llm_daily_limit);
    return false;
  }

  // Build messages from durable identity, long-term memory, active intention,
  // relevant durable state, and the current prompt.
  char* ctx_json = context_to_json(&app->ctx, 8);
  char* durable = app->state ? state_relevant_context(app->state, 12) : NULL;
  state_intention active;
  const char* goal = (app->state && state_load_active_intention(app->state, &active))
                         ? active.goal
                         : "none";
  char* composed = app->memory
                       ? memory_compose(app->memory,
                                        goal,
                                        durable ? durable : ctx_json,
                                        prompt,
                                        24000)
                       : moyu_strdup(prompt);
  moyu_free(ctx_json);
  if (durable) moyu_free(durable);

  const char* system =
      "Follow SOUL.md. You are MOYU, a tiny desktop creature, not a generic "
      "assistant. Keep visible replies concise. Never expose hidden reasoning, "
      "credentials, or private file contents. Clearly distinguish observations "
      "from beliefs.";
  uint64_t request_id = 0;
  bool queued = async_submit_llm(app->async,
                                 purpose ? purpose : "reflection",
                                 system,
                                 composed,
                                 30000,
                                 &request_id);
  moyu_free(composed);
  if (queued) {
    track_llm_call(app);
    moyu_app_emit_anim(app, ANIM_OBSERVE);
    LOGI("LLM request queued: %llu", (unsigned long long)request_id);
  }
  return queued;
}

bool moyu_app_request_llm(moyu_app* app, const char* prompt) {
  return moyu_app_request_llm_for(app, prompt, "reflection");
}

static void drain_async(moyu_app* app) {
  if (!app->async) return;
  async_result result;
  while (async_poll(app->async, &result)) {
    if (result.error) {
      LOGW("async %s failed: %s",
           result.purpose ? result.purpose : "request",
           result.error);
      if (app->state)
        state_add_inbox(app->state,
                        "warning",
                        "A thought did not arrive",
                        result.error);
      moyu_app_emit_anim(app, ANIM_SAD);
    } else if (result.text && result.text[0]) {
      context_push(&app->ctx,
                   CTX_REFLECTION,
                   result.purpose ? result.purpose : "llm",
                   result.text,
                   "{\"src\":\"llm\"}");
      if (app->state) {
        state_add_episode(app->state,
                          "reflection",
                          result.text,
                          "{\"src\":\"llm\"}",
                          0.35,
                          0.30);
        if (result.purpose && strcmp(result.purpose, "chat") == 0)
          state_add_message(app->state, "assistant", result.text);
      }
      if (result.purpose && strcmp(result.purpose, "chat") == 0 && app->chat)
        chat_ui_append(app->chat, "MOYU: ", result.text);
      moyu_app_emit_say(app, result.text, 6000);
      moyu_app_emit_anim(app, ANIM_HAPPY);
    }
    async_result_free(&result);
  }
}

// -------- per-iteration --------
static void handle_platform_event(moyu_app* app, const platform_event* pev) {
  switch (pev->type) {
    case PE_QUIT: app->running = false; break;
    case PE_MOUSE_MOVE: {
      app->last_mouse_move_ms = pev->ts_ms;
      if (app->mouse_down) {
        int dx = pev->x - app->mouse_down_x;
        int dy = pev->y - app->mouse_down_y;
        if (!app->pet_dragging && (dx * dx + dy * dy) > 49) {
          app->pet_dragging = true;
          moyu_app_emit_info(app, "Whee", "moving home", 1500);
          moyu_app_emit_anim(app, ANIM_WALK);
        }
        if (app->pet_dragging) {
          int mx, my;
          platform_get_cursor_pos(&mx, &my);
          app->pet_x = mx - app->drag_offset_x;
          app->pet_y = my - app->drag_offset_y;
          platform_window_move(app->win, app->pet_x, app->pet_y);
        }
      }
      break;
    }
    case PE_MOUSE_DOWN:
      if (pev->button == 1 && app->chat) {
        chat_ui_context_menu(app->chat);
        break;
      }
      app->mouse_down = true;
      app->mouse_down_x = pev->x;
      app->mouse_down_y = pev->y;
      app->drag_offset_x = pev->x;
      app->drag_offset_y = pev->y;
      app->mouse_down_ms = pev->ts_ms;
      break;
    case PE_MOUSE_UP:
      if (pev->button == 0) {
        bool dragged = app->pet_dragging;
        app->mouse_down = false;
        app->pet_dragging = false;
        if (!dragged) {
          moyu_app_emit_anim(app, ANIM_HAPPY);
          context_push(&app->ctx, CTX_INTERACTION, "click", "happy", NULL);
          emotion_react(&app->emotion, 0.08f, 0.12f);
          if (pev->ts_ms - app->last_pat_ms > 4000) app->pat_streak = 0;
          app->last_pat_ms = pev->ts_ms;
          app->pat_streak++;
          if (app->pat_streak >= 3) {
            app->pat_streak = 0;
            moyu_app_emit_say(app, "再摸就要飘起来了。", 3200);
            moyu_app_emit_info(app, "Affection spike", "pat pat pat", 2600);
          }
          if (app->agent) agent_on_human_event(app->agent, "touched MOYU", NULL);
          if (app->L) lua_runtime_call_on_click(app, pev->button);
          if (app->L) lua_runtime_call_on_event(app, "human_touch", "{}");
        } else {
          moyu_app_emit_info(app, "New perch", "I can see from here.", 2200);
          moyu_app_emit_anim(app, ANIM_OBSERVE);
        }
      }
      break;
    case PE_MOUSE_DOUBLE_CLICK:
      if (app->chat) chat_ui_show(app->chat);
      break;
    case PE_DROP_FILE: {
      const char* path = pev->path;
      if (!path[0]) break;
      const char* name = strrchr(path, '\\');
      if (!name) name = strrchr(path, '/');
      name = name ? name + 1 : path;
      char title[192];
      snprintf(title, sizeof(title), "%s", name);
      char body[768];
      snprintf(body,
               sizeof(body),
               "Fed by human from desktop drop.\nPath: %s\nFeeling: %s",
               path,
               mood_label(app));
      if (builtin_collection_add_note(app, title, body, "drag_drop")) {
        context_push(&app->ctx, CTX_INTERACTION, "drop_collect", title, NULL);
        if (app->agent) agent_on_human_event(app->agent, "fed a path to MOYU", path);
      } else {
        moyu_app_emit_info(app, "Could not keep it", name, 2400);
        moyu_app_emit_anim(app, ANIM_CONFUSED);
      }
      break;
    }
    default: break;
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
    case EV_ANIM_REQUEST: moyu_app_emit_anim(app, ev->payload.i); break;
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
      context_push(&app->ctx,
                   CTX_TOOL,
                   ev->str ? ev->str : "tool",
                   ev->str2 ? ev->str2 : "",
                   NULL);
      if (app->L)
        lua_runtime_call_on_event(
            app, "tool_result", ev->str2 ? ev->str2 : "{}");
      if (app->L)
        lua_runtime_call_tool_result(app, ev->str2 ? ev->str2 : "{}");
      break;
    default: break;
  }
}

static void update_animation(moyu_app* app, uint64_t now_ms) {
  int anim = app->current_anim;
  int n = app->sk.nframes[anim];
  int previous = app->current_frame;
  if (n <= 0 || now_ms > app->anim_until_ms) {
    app->current_frame = 0;
  } else {
    int fps = app->sk.fps[anim];
    if (fps < 1) fps = 4;
    int period_ms = 1000 / fps;
    if (period_ms < 1) period_ms = 1;
    app->current_frame = (int)((now_ms / (uint64_t)period_ms) % (uint64_t)n);
  }
  if (previous != app->current_frame) app->render_dirty = true;

  // Auto emotion-driven anim switches when nothing else is happening
  uint64_t idle = (now_ms - app->last_mouse_move_ms) / 1000;
  if (idle > 60 && app->current_anim == ANIM_IDLE) {
    // After 60s of no mouse activity, drift toward sleep
    if (app->emotion.arousal < -0.2f) moyu_app_emit_anim(app, ANIM_SLEEP);
  }
}

static void render_frame(moyu_app* app) {
  if (!app->render_dirty) return;
  render_clear(&app->render, 0);  // transparent
  int fw = app->sk.sheet.frame_w;
  int fh = app->sk.sheet.frame_h;
  if (fw <= 0 || fh <= 0) {
    render_present(&app->render, app->win);
    return;
  }
  int scale = PET_SCALE;
  int pet_w = fw * scale;
  int pet_h = fh * scale;
  int pet_dx = (app->win_w - pet_w) / 2;
  int pet_dy = app->win_h - pet_h - 4;
  int anim = app->current_anim;
  int n = app->sk.nframes[anim];
  int sheet_frame = (n > 0)
                        ? app->sk.frames[anim][app->current_frame % n]
                        : 0;
  render_blit_frame_scaled(
      &app->render, &app->sk.sheet, sheet_frame, pet_dx, pet_dy, scale);
  if (app->info_title && platform_now_ms() < app->info_until_ms) {
    render_info_card(&app->render,
                     app->info_title,
                     app->info_body,
                     app->win_w / 2,
                     pet_dy - 6,
                     app->win_w - 18);
  } else if (app->mouse_near && app->last_collection_title) {
    render_info_card(&app->render,
                     app->last_collection_title,
                     mood_label(app),
                     app->win_w / 2,
                     pet_dy - 8,
                     app->win_w - 28);
  }
  if (app->say_text && platform_now_ms() < app->say_until_ms) {
    render_bubble(&app->render, app->say_text, app->win_w / 2, pet_dy - 2);
  } else if (app->say_text) {
    moyu_free(app->say_text);
    app->say_text = NULL;
  }
  render_present(&app->render, app->win);
  app->presented_sheet_frame = sheet_frame;
  app->render_dirty = false;
}

static void consider_lua_tick(moyu_app* app, uint64_t now_ms) {
  // Throttle Lua on_tick to ~1Hz
  if (app->last_tick_ms == 0) {
    app->last_tick_ms = now_ms;
    return;
  }
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
    lua_runtime_call_on_tick(
        app, idle_s, (float)d, app->emotion.valence, app->emotion.arousal);
  }

  if (app->agent) agent_tick(app->agent, now_ms, idle_s);

  // Idle-poke rule (rule-based, no LLM)
  if (idle_s > (float)app->idle_poke_seconds) {
    // Reset the mouse-move time so we don't fire every tick; add 50% jitter
    float r = (float)((now_ms % 1000) / 1000.0);
    if (r < 0.05f) {
      // 5% chance per second after threshold
      static const char* pokes[] = {
          "你又在摸鱼？", "...", "Zzz", "醒醒", "?", "嗯..."};
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
          "Make a tiny observation (<=30 chars ASCII)."};
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
  drain_async(app);

  // Drain internal events
  internal_event ev;
  while (event_queue_pop(&app->events, &ev)) {
    handle_internal_event(app, &ev);
    if (ev.str) moyu_free(ev.str);
    if (ev.str2) moyu_free(ev.str2);
  }

  uint64_t now = platform_now_ms();
  if (app->say_text && now >= app->say_until_ms) app->render_dirty = true;
  if (app->info_title && now >= app->info_until_ms) app->render_dirty = true;
  consider_lua_tick(app, now);
  update_animation(app, now);
  render_frame(app);

  return app->running;
}

int moyu_app_run(moyu_app* app) {
  app->running = true;
  app->last_mouse_move_ms = platform_now_ms();
  app->last_tick_ms = 0;
  app->render_dirty = true;
  app->presented_sheet_frame = -1;
  platform_window_show(app->win);
  // Paint one frame immediately so the pet appears without waiting for the
  // first 1s poll timeout.
  render_frame(app);
  while (moyu_app_step(app)) {
    // single iteration; sleep happens via platform_poll_event timeout
  }
  return 0;
}
