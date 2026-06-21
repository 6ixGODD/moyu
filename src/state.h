#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct sqlite3 sqlite3;

typedef struct state_store {
  sqlite3* db;
  char* path;
} state_store;

typedef struct {
  int64_t id;
  char goal[256];
  char status[32];
  int step_index;
  int max_steps;
  uint64_t deadline_ms;
} state_intention;

bool state_open(state_store* s, const char* path);
void state_close(state_store* s);
bool state_transaction(state_store* s, const char* sql);

int64_t state_add_episode(state_store* s,
                          const char* kind,
                          const char* summary,
                          const char* metadata_json,
                          double importance,
                          double novelty);
bool state_mark_episode_promoted(state_store* s, int64_t id);
bool state_upsert_belief(state_store* s,
                         const char* subject,
                         const char* predicate,
                         const char* object,
                         double confidence,
                         const char* source);
bool state_correct_belief(state_store* s, const char* subject, const char* note);
char* state_relevant_context(state_store* s, int max_items);

int64_t state_create_intention(state_store* s,
                               const char* goal,
                               const char* source,
                               int max_steps,
                               uint64_t deadline_ms);
bool state_update_intention(state_store* s,
                            int64_t id,
                            const char* status,
                            int step_index,
                            const char* result);
bool state_load_active_intention(state_store* s, state_intention* out);

bool state_permission_set(state_store* s,
                          const char* tool,
                          const char* scope,
                          const char* decision,
                          bool persistent);
bool state_permission_allowed(state_store* s,
                              const char* tool,
                              const char* scope,
                              const char* risk);
bool state_add_message(state_store* s, const char* role, const char* content);
bool state_add_inbox(state_store* s,
                     const char* severity,
                     const char* title,
                     const char* body);
bool state_record_feedback(state_store* s,
                           const char* domain,
                           const char* kind);
bool state_budget_take(state_store* s,
                       const char* bucket,
                       int daily_limit,
                       int amount);
bool state_cleanup(state_store* s, int retention_days);
char* state_meta_get(state_store* s, const char* key);
bool state_meta_set(state_store* s, const char* key, const char* value);
