#include "state.h"

#include "cJSON.h"
#include "log.h"
#include "mem.h"
#include "platform.h"
#include "sqlite3.h"

#include <stdio.h>
#include <string.h>

#define SCHEMA_VERSION 1

static bool exec_sql(sqlite3* db, const char* sql) {
  char* err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK) {
    LOGE("sqlite: %s", err ? err : sqlite3_errmsg(db));
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool state_transaction(state_store* s, const char* sql) {
  return s && s->db && exec_sql(s->db, sql);
}

static bool migrate(sqlite3* db) {
  static const char* schema =
      "BEGIN IMMEDIATE;"
      "CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);"
      "CREATE TABLE IF NOT EXISTS episodes("
      " id INTEGER PRIMARY KEY,kind TEXT NOT NULL,summary TEXT NOT NULL,"
      " metadata TEXT,importance REAL NOT NULL DEFAULT 0,novelty REAL NOT NULL DEFAULT 0,"
      " promoted INTEGER NOT NULL DEFAULT 0,created_at INTEGER NOT NULL);"
      "CREATE INDEX IF NOT EXISTS episodes_created ON episodes(created_at);"
      "CREATE TABLE IF NOT EXISTS beliefs("
      " id INTEGER PRIMARY KEY,subject TEXT NOT NULL,predicate TEXT NOT NULL,"
      " object TEXT NOT NULL,confidence REAL NOT NULL,source TEXT,updated_at INTEGER NOT NULL,"
      " UNIQUE(subject,predicate));"
      "CREATE TABLE IF NOT EXISTS drives("
      " name TEXT PRIMARY KEY,value REAL NOT NULL,last_trigger INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS habits("
      " name TEXT PRIMARY KEY,weight REAL NOT NULL,evidence INTEGER NOT NULL DEFAULT 0,"
      " updated_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS relationships("
      " domain TEXT PRIMARY KEY,allowed INTEGER NOT NULL DEFAULT 0,denied INTEGER NOT NULL DEFAULT 0,"
      " corrected INTEGER NOT NULL DEFAULT 0,updated_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS intentions("
      " id INTEGER PRIMARY KEY,goal TEXT NOT NULL,source TEXT,status TEXT NOT NULL,"
      " step_index INTEGER NOT NULL DEFAULT 0,max_steps INTEGER NOT NULL DEFAULT 3,"
      " deadline_ms INTEGER NOT NULL,result TEXT,created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS intention_steps("
      " id INTEGER PRIMARY KEY,intention_id INTEGER NOT NULL,step_no INTEGER NOT NULL,"
      " tool TEXT,input TEXT,status TEXT,idempotency_key TEXT UNIQUE,result TEXT,"
      " created_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS permissions("
      " id INTEGER PRIMARY KEY,tool TEXT NOT NULL,scope TEXT NOT NULL,decision TEXT NOT NULL,"
      " persistent INTEGER NOT NULL,updated_at INTEGER NOT NULL,UNIQUE(tool,scope));"
      "CREATE TABLE IF NOT EXISTS tool_audit("
      " id INTEGER PRIMARY KEY,request_id TEXT,intention_id INTEGER,tool TEXT,risk TEXT,scope TEXT,"
      " status TEXT,summary TEXT,started_at INTEGER,finished_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mcp_servers("
      " id INTEGER PRIMARY KEY,name TEXT UNIQUE,transport TEXT,config TEXT,status TEXT,last_error TEXT,updated_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mcp_tools("
      " id INTEGER PRIMARY KEY,server_id INTEGER,name TEXT,description TEXT,input_schema TEXT,"
      " risk TEXT,affordance TEXT,UNIQUE(server_id,name));"
      "CREATE TABLE IF NOT EXISTS chat_sessions("
      " id INTEGER PRIMARY KEY,started_at INTEGER NOT NULL,updated_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS chat_messages("
      " id INTEGER PRIMARY KEY,session_id INTEGER,role TEXT NOT NULL,content TEXT NOT NULL,created_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS inbox("
      " id INTEGER PRIMARY KEY,severity TEXT,title TEXT,body TEXT,read INTEGER NOT NULL DEFAULT 0,created_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS collections("
      " id INTEGER PRIMARY KEY,title TEXT,body TEXT,source TEXT,created_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS budgets("
      " bucket TEXT NOT NULL,day INTEGER NOT NULL,used INTEGER NOT NULL,PRIMARY KEY(bucket,day));"
      "INSERT INTO meta(key,value) VALUES('schema_version','1') "
      "ON CONFLICT(key) DO UPDATE SET value='1';"
      "INSERT OR IGNORE INTO drives(name,value,last_trigger) VALUES"
      "('curiosity',0.35,0),('boredom',0.15,0),('attachment',0.25,0),('collection',0.30,0);"
      "COMMIT;";
  return exec_sql(db, schema);
}

bool state_open(state_store* s, const char* path) {
  if (!s || !path) return false;
  memset(s, 0, sizeof(*s));
  int rc = sqlite3_open_v2(path,
                           &s->db,
                           SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                               SQLITE_OPEN_FULLMUTEX,
                           NULL);
  if (rc != SQLITE_OK) {
    LOGE("sqlite open %s: %s", path, s->db ? sqlite3_errmsg(s->db) : "failed");
    state_close(s);
    return false;
  }
  s->path = moyu_strdup(path);
  sqlite3_busy_timeout(s->db, 3000);
  exec_sql(s->db, "PRAGMA foreign_keys=ON;PRAGMA journal_mode=WAL;PRAGMA synchronous=NORMAL;");
  if (!migrate(s->db)) {
    sqlite3_close(s->db);
    s->db = NULL;
    char corrupt[4096];
    snprintf(corrupt,
             sizeof(corrupt),
             "%s.corrupt-%llu",
             path,
             (unsigned long long)platform_unix_ms());
    if (!platform_move_file(path, corrupt, false)) {
      state_close(s);
      return false;
    }
    LOGW("state database was preserved as %s; creating a clean database",
         corrupt);
    if (sqlite3_open_v2(path,
                        &s->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_FULLMUTEX,
                        NULL) != SQLITE_OK ||
        !migrate(s->db)) {
      state_close(s);
      return false;
    }
  }
  return true;
}

void state_close(state_store* s) {
  if (!s) return;
  if (s->db) sqlite3_close(s->db);
  if (s->path) moyu_free(s->path);
  memset(s, 0, sizeof(*s));
}

static bool bind_text(sqlite3_stmt* st, int i, const char* v) {
  return sqlite3_bind_text(st, i, v ? v : "", -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

int64_t state_add_episode(state_store* s,
                          const char* kind,
                          const char* summary,
                          const char* metadata_json,
                          double importance,
                          double novelty) {
  if (!s || !s->db || !summary) return 0;
  sqlite3_stmt* st = NULL;
  const char* sql = "INSERT INTO episodes(kind,summary,metadata,importance,novelty,created_at) VALUES(?,?,?,?,?,?)";
  if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  bind_text(st, 1, kind);
  bind_text(st, 2, summary);
  if (metadata_json)
    bind_text(st, 3, metadata_json);
  else
    sqlite3_bind_null(st, 3);
  sqlite3_bind_double(st, 4, importance);
  sqlite3_bind_double(st, 5, novelty);
  sqlite3_bind_int64(st, 6, (sqlite3_int64)platform_unix_ms());
  int ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok ? sqlite3_last_insert_rowid(s->db) : 0;
}

bool state_mark_episode_promoted(state_store* s, int64_t id) {
  sqlite3_stmt* st = NULL;
  if (!s || sqlite3_prepare_v2(s->db, "UPDATE episodes SET promoted=1 WHERE id=?", -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_int64(st, 1, id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool state_upsert_belief(state_store* s,
                         const char* subject,
                         const char* predicate,
                         const char* object,
                         double confidence,
                         const char* source) {
  sqlite3_stmt* st = NULL;
  const char* sql = "INSERT INTO beliefs(subject,predicate,object,confidence,source,updated_at) VALUES(?,?,?,?,?,?) "
                    "ON CONFLICT(subject,predicate) DO UPDATE SET object=excluded.object,confidence=excluded.confidence,source=excluded.source,updated_at=excluded.updated_at";
  if (!s || sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  bind_text(st, 1, subject); bind_text(st, 2, predicate); bind_text(st, 3, object);
  sqlite3_bind_double(st, 4, confidence); bind_text(st, 5, source);
  sqlite3_bind_int64(st, 6, (sqlite3_int64)platform_unix_ms());
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

bool state_correct_belief(state_store* s, const char* subject, const char* note) {
  sqlite3_stmt* st = NULL;
  const char* sql = "UPDATE beliefs SET confidence=MAX(0,confidence-0.35),source=?,updated_at=? WHERE subject LIKE ?";
  if (!s || sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  bind_text(st, 1, note); sqlite3_bind_int64(st, 2, (sqlite3_int64)platform_unix_ms());
  char pat[320]; snprintf(pat, sizeof(pat), "%%%s%%", subject ? subject : ""); bind_text(st, 3, pat);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

char* state_relevant_context(state_store* s, int max_items) {
  if (!s || !s->db) return moyu_strdup("[]");
  sqlite3_stmt* st = NULL;
  const char* sql = "SELECT 'belief',subject||' '||predicate||' '||object,confidence FROM beliefs "
                    "UNION ALL SELECT 'episode',summary,importance FROM episodes ORDER BY 3 DESC LIMIT ?";
  if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return moyu_strdup("[]");
  sqlite3_bind_int(st, 1, max_items > 0 ? max_items : 8);
  cJSON* array = cJSON_CreateArray();
  while (sqlite3_step(st) == SQLITE_ROW) {
    const char* kind = (const char*)sqlite3_column_text(st, 0);
    const char* text = (const char*)sqlite3_column_text(st, 1);
    double weight = sqlite3_column_double(st, 2);
    cJSON* row = cJSON_CreateObject();
    cJSON_AddStringToObject(row, "kind", kind ? kind : "");
    cJSON_AddStringToObject(row, "text", text ? text : "");
    cJSON_AddNumberToObject(row, "weight", weight);
    cJSON_AddItemToArray(array, row);
  }
  sqlite3_finalize(st);
  char* out = cJSON_PrintUnformatted(array);
  cJSON_Delete(array);
  return out;
}

int64_t state_create_intention(state_store* s,
                               const char* goal,
                               const char* source,
                               int max_steps,
                               uint64_t deadline_ms) {
  if (!s) return 0;
  exec_sql(s->db, "UPDATE intentions SET status='abandoned',result='superseded' WHERE status IN('pending','running','waiting_permission');");
  sqlite3_stmt* st = NULL;
  const char* sql = "INSERT INTO intentions(goal,source,status,max_steps,deadline_ms,created_at,updated_at) VALUES(?,?,'pending',?,?,?,?)";
  if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  uint64_t now = platform_unix_ms();
  bind_text(st, 1, goal); bind_text(st, 2, source); sqlite3_bind_int(st, 3, max_steps);
  sqlite3_bind_int64(st, 4, (sqlite3_int64)deadline_ms); sqlite3_bind_int64(st, 5, (sqlite3_int64)now); sqlite3_bind_int64(st, 6, (sqlite3_int64)now);
  bool ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st);
  return ok ? sqlite3_last_insert_rowid(s->db) : 0;
}

bool state_update_intention(state_store* s, int64_t id, const char* status, int step_index, const char* result) {
  sqlite3_stmt* st = NULL;
  const char* sql = "UPDATE intentions SET status=?,step_index=?,result=?,updated_at=? WHERE id=?";
  if (!s || sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  bind_text(st, 1, status); sqlite3_bind_int(st, 2, step_index);
  if (result) bind_text(st, 3, result); else sqlite3_bind_null(st, 3);
  sqlite3_bind_int64(st, 4, (sqlite3_int64)platform_unix_ms()); sqlite3_bind_int64(st, 5, id);
  bool ok = sqlite3_step(st) == SQLITE_DONE; sqlite3_finalize(st); return ok;
}

bool state_load_active_intention(state_store* s, state_intention* out) {
  if (!s || !out) return false;
  memset(out, 0, sizeof(*out));
  sqlite3_stmt* st = NULL;
  const char* sql = "SELECT id,goal,status,step_index,max_steps,deadline_ms FROM intentions WHERE status IN('pending','running','waiting_permission') ORDER BY id DESC LIMIT 1";
  if (sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  bool found = sqlite3_step(st) == SQLITE_ROW;
  if (found) {
    out->id = sqlite3_column_int64(st, 0);
    snprintf(out->goal, sizeof(out->goal), "%s", sqlite3_column_text(st, 1));
    snprintf(out->status, sizeof(out->status), "%s", sqlite3_column_text(st, 2));
    out->step_index = sqlite3_column_int(st, 3); out->max_steps = sqlite3_column_int(st, 4);
    out->deadline_ms = (uint64_t)sqlite3_column_int64(st, 5);
  }
  sqlite3_finalize(st); return found;
}

bool state_permission_set(state_store* s, const char* tool, const char* scope, const char* decision, bool persistent) {
  sqlite3_stmt* st = NULL;
  const char* sql = "INSERT INTO permissions(tool,scope,decision,persistent,updated_at) VALUES(?,?,?,?,?) ON CONFLICT(tool,scope) DO UPDATE SET decision=excluded.decision,persistent=excluded.persistent,updated_at=excluded.updated_at";
  if (!s || sqlite3_prepare_v2(s->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  bind_text(st,1,tool); bind_text(st,2,scope); bind_text(st,3,decision); sqlite3_bind_int(st,4,persistent?1:0); sqlite3_bind_int64(st,5,(sqlite3_int64)platform_unix_ms());
  bool ok=sqlite3_step(st)==SQLITE_DONE; sqlite3_finalize(st); return ok;
}

bool state_permission_allowed(state_store* s, const char* tool, const char* scope, const char* risk) {
  if (!s || !strcmp(risk ? risk : "", "mutate")) return false;
  sqlite3_stmt* st=NULL;
  const char* sql="SELECT decision FROM permissions WHERE tool=? AND (scope=? OR scope='*') ORDER BY scope<>'*' DESC LIMIT 1";
  if (sqlite3_prepare_v2(s->db,sql,-1,&st,NULL)!=SQLITE_OK) return false;
  bind_text(st,1,tool); bind_text(st,2,scope); bool ok=false;
  if(sqlite3_step(st)==SQLITE_ROW){ const char* d=(const char*)sqlite3_column_text(st,0); ok=d && !strcmp(d,"allow"); }
  sqlite3_finalize(st); return ok;
}

bool state_add_message(state_store* s, const char* role, const char* content) {
  sqlite3_stmt* st=NULL;
  const char* sql="INSERT INTO chat_messages(session_id,role,content,created_at) VALUES(1,?,?,?)";
  exec_sql(s->db,"INSERT OR IGNORE INTO chat_sessions(id,started_at,updated_at) VALUES(1,0,0);");
  if(sqlite3_prepare_v2(s->db,sql,-1,&st,NULL)!=SQLITE_OK)return false;
  bind_text(st,1,role); bind_text(st,2,content); sqlite3_bind_int64(st,3,(sqlite3_int64)platform_unix_ms());
  bool ok=sqlite3_step(st)==SQLITE_DONE; sqlite3_finalize(st); return ok;
}

bool state_add_inbox(state_store* s,const char* severity,const char* title,const char* body){
  sqlite3_stmt* st=NULL; const char* sql="INSERT INTO inbox(severity,title,body,created_at) VALUES(?,?,?,?)";
  if(!s||sqlite3_prepare_v2(s->db,sql,-1,&st,NULL)!=SQLITE_OK)return false;
  bind_text(st,1,severity);bind_text(st,2,title);bind_text(st,3,body);sqlite3_bind_int64(st,4,(sqlite3_int64)platform_unix_ms());
  bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok;
}

bool state_record_feedback(state_store* s,const char* domain,const char* kind){
  if(!s||!domain||!kind)return false;
  const char* col=!strcmp(kind,"allow")?"allowed":(!strcmp(kind,"deny")?"denied":"corrected");
  char sql[512];snprintf(sql,sizeof(sql),
      "INSERT INTO relationships(domain,allowed,denied,corrected,updated_at) VALUES(?,0,0,0,?) "
      "ON CONFLICT(domain) DO UPDATE SET %s=%s+1,updated_at=excluded.updated_at",col,col);
  sqlite3_stmt* st=NULL;if(sqlite3_prepare_v2(s->db,sql,-1,&st,NULL)!=SQLITE_OK)return false;
  bind_text(st,1,domain);sqlite3_bind_int64(st,2,(sqlite3_int64)platform_unix_ms());
  bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok;
}

bool state_budget_take(state_store* s,const char* bucket,int daily_limit,int amount){
  if(!s||daily_limit<=0||amount<=0)return false;
  int64_t day=(int64_t)(platform_unix_ms()/86400000ULL); sqlite3_stmt* st=NULL;
  const char* q="SELECT used FROM budgets WHERE bucket=? AND day=?";
  if(sqlite3_prepare_v2(s->db,q,-1,&st,NULL)!=SQLITE_OK)return false;bind_text(st,1,bucket);sqlite3_bind_int64(st,2,day);
  int used=0;if(sqlite3_step(st)==SQLITE_ROW)used=sqlite3_column_int(st,0);sqlite3_finalize(st);if(used+amount>daily_limit)return false;
  const char* u="INSERT INTO budgets(bucket,day,used) VALUES(?,?,?) ON CONFLICT(bucket,day) DO UPDATE SET used=used+excluded.used";
  if(sqlite3_prepare_v2(s->db,u,-1,&st,NULL)!=SQLITE_OK)return false;bind_text(st,1,bucket);sqlite3_bind_int64(st,2,day);sqlite3_bind_int(st,3,amount);
  bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok;
}

bool state_cleanup(state_store* s,int retention_days){
  if(!s||retention_days<1)return false;
  uint64_t cutoff=platform_unix_ms()-(uint64_t)retention_days*86400000ULL;
  char sql[768];snprintf(sql,sizeof(sql),
      "BEGIN;DELETE FROM episodes WHERE promoted=0 AND created_at<%llu;"
      "DELETE FROM chat_messages WHERE created_at<%llu;"
      "DELETE FROM tool_audit WHERE finished_at IS NOT NULL AND finished_at<%llu;COMMIT;",
      (unsigned long long)cutoff,(unsigned long long)cutoff,(unsigned long long)cutoff);
  return exec_sql(s->db,sql);
}

char* state_meta_get(state_store* s,const char* key){
  if(!s||!key)return NULL;sqlite3_stmt* st=NULL;
  if(sqlite3_prepare_v2(s->db,"SELECT value FROM meta WHERE key=?",-1,&st,NULL)!=SQLITE_OK)return NULL;
  bind_text(st,1,key);char* out=NULL;if(sqlite3_step(st)==SQLITE_ROW){const char* v=(const char*)sqlite3_column_text(st,0);if(v)out=moyu_strdup(v);}sqlite3_finalize(st);return out;
}

bool state_meta_set(state_store* s,const char* key,const char* value){
  if(!s||!key)return false;sqlite3_stmt* st=NULL;const char* sql="INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value";
  if(sqlite3_prepare_v2(s->db,sql,-1,&st,NULL)!=SQLITE_OK)return false;bind_text(st,1,key);bind_text(st,2,value);bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);return ok;
}
