#pragma once
#include "context.h"
#include "desktop_os.h"
#include "emotion.h"
#include "event.h"
#include "persona.h"
#include "platform.h"
#include "render.h"
#include "sprite.h"

#include <stdbool.h>
#include <stdint.h>

// Forward declarations to avoid include cycles.
struct lua_State;
struct llm_config;
struct llm_cache;
struct tool_registry;
struct state_store;
struct memory_system;
struct moyu_workdir;
struct agent_runtime;
struct async_worker;
struct chat_ui;
struct mcp_client;

// Integer scale applied to the skin's frame when blitting. The pet occupies
// (skin.frame_w * PET_SCALE) x (skin.frame_h * PET_SCALE) px, bottom-centered
// in the window. Bubbles render in the space above it. Bumped from 3 so the
// default 32px skin is clearly visible (32*3 = 96px).
#define PET_SCALE 2

typedef enum {
  PET_BEHAVIOR_NONE = 0,
  PET_BEHAVIOR_ROAM,
  PET_BEHAVIOR_SNEAK,
  PET_BEHAVIOR_PEEK,
  PET_BEHAVIOR_BORED,
  PET_BEHAVIOR_PLAY,
  PET_BEHAVIOR_DODGE,
  PET_BEHAVIOR_STRETCH,
  PET_BEHAVIOR_YAWN,
} t_pet_behavior_kind;

typedef struct {
  t_pet_behavior_kind kind;
  int anim_id;
  int target_x;
  int target_y;
  float speed_px_s;
  uint64_t started_ms;
  uint64_t until_ms;
  uint64_t next_ms;
  uint64_t last_step_ms;
  uint64_t pointer_cooldown_ms;
} t_pet_behavior;

typedef struct moyu_app {
  // platform
  platform_window* win;
  int win_w, win_h;
  int pet_x, pet_y;  // screen coords of pet's top-left

  // render
  render_ctx render;
  skin sk;  // appearance: one spritesheet + per-anim frame tables (owned)

  // core state
  context_store ctx;
  personality personality;
  emotion emotion;
  event_queue events;

  // script
  struct lua_State* L;

  // llm
  struct llm_config* llm;
  struct llm_config* vision_llm;
  struct llm_cache* cache;

  // tools
  struct tool_registry* tools;
  struct state_store* state;
  struct memory_system* memory;
  struct moyu_workdir* workdir;
  struct agent_runtime* agent;
  struct async_worker* async;
  struct chat_ui* chat;
  char* observe_root;
  struct mcp_client** mcp_clients;
  size_t mcp_client_count;
  size_t mcp_client_cap;

  // runtime
  int current_anim;
  int current_frame;
  int presented_sheet_frame;
  int frame_counter;  // increments every iteration; modulo used for frame rate
  uint64_t anim_until_ms;
  bool render_dirty;
  uint64_t last_tick_ms;
  uint64_t last_mouse_move_ms;
  int last_mouse_distance;
  bool mouse_near;
  bool mouse_down;
  bool pet_dragging;
  int mouse_down_x;
  int mouse_down_y;
  int drag_offset_x;
  int drag_offset_y;
  uint64_t mouse_down_ms;
  uint64_t last_pat_ms;
  int pat_streak;
  bool facing_left;
  t_pet_behavior behavior;

  // say bubble
  char* say_text;  // owned
  uint64_t say_until_ms;
  char* info_title;
  char* info_body;
  uint64_t info_until_ms;
  char* last_collection_title;
  char* last_collection_body;

  // LLM throttle: ring of recent call timestamps (ms)
  uint64_t llm_calls[128];
  size_t llm_calls_count, llm_calls_head;

  // config: behaviour thresholds
  int idle_poke_seconds;
  float rare_event_chance;
  int llm_daily_limit;
  bool llm_enabled;  // false if api_key empty
  bool vision_enabled;
  t_system_snapshot system_snapshot;
  t_owner_profile owner_profile;
  char* pending_drop_title;

  bool running;
} moyu_app;

// One iteration of the main loop. Returns false on EV_QUIT.
bool moyu_app_step(moyu_app* app);

// Block-running wrapper.
int moyu_app_run(moyu_app* app);

// Helpers used by Lua bindings:
void moyu_app_emit_say(moyu_app* app, const char* text, int duration_ms);
void moyu_app_emit_anim(moyu_app* app, int anim_id);
void moyu_app_emit_info(moyu_app* app,
                        const char* title,
                        const char* body,
                        int duration_ms);
void moyu_app_note_collection(moyu_app* app,
                              const char* title,
                              const char* body);
bool moyu_app_request_llm(moyu_app* app,
                          const char* prompt);
bool moyu_app_request_llm_for(moyu_app* app,
                              const char* prompt,
                              const char* purpose);
bool moyu_app_send_chat(moyu_app* app, const char* text);
