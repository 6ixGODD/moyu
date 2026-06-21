#include "async.h"

#include "llm.h"
#include "mem.h"
#include "platform.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct {
  async_kind kind;
  uint64_t id;
  char* purpose;
  char* system;
  char* user;
  int timeout_ms;
} async_job;

struct async_worker {
  HANDLE thread;
  CRITICAL_SECTION lock;
  CONDITION_VARIABLE ready;
  bool stop;
  bool has_job;
  bool has_result;
  bool working;
  uint64_t next_id;
  async_job job;
  async_result result;
  llm_config* llm;
  platform_window* wake_window;
};

static void job_free(async_job* j) {
  if (j->purpose) moyu_free(j->purpose);
  if (j->system) moyu_free(j->system);
  if (j->user) moyu_free(j->user);
  memset(j, 0, sizeof(*j));
}

void async_result_free(async_result* r) {
  if (!r) return;
  if (r->purpose) moyu_free(r->purpose);
  if (r->text) moyu_free(r->text);
  if (r->error) moyu_free(r->error);
  memset(r, 0, sizeof(*r));
}

static DWORD WINAPI worker_main(void* p) {
  async_worker* w = (async_worker*)p;
  for (;;) {
    EnterCriticalSection(&w->lock);
    while (!w->stop && !w->has_job)
      SleepConditionVariableCS(&w->ready, &w->lock, INFINITE);
    if (w->stop) {
      LeaveCriticalSection(&w->lock);
      break;
    }
    async_job job = w->job;
    memset(&w->job, 0, sizeof(w->job));
    w->has_job = false;
    w->working = true;
    LeaveCriticalSection(&w->lock);

    async_result result = {0};
    result.kind = job.kind;
    result.id = job.id;
    result.purpose = moyu_strdup(job.purpose ? job.purpose : "reflection");
    if (job.kind == ASYNC_LLM) {
      const char* messages[2] = {job.system, job.user};
      llm_result lr = llm_complete(w->llm, NULL, messages, 2, job.timeout_ms);
      result.text = lr.text;
      result.error = lr.error;
      result.status = lr.status;
      lr.text = lr.error = NULL;
      llm_result_free(&lr);
    }
    job_free(&job);

    EnterCriticalSection(&w->lock);
    if (w->has_result) async_result_free(&w->result);
    w->result = result;
    w->has_result = true;
    w->working = false;
    LeaveCriticalSection(&w->lock);
    platform_window_wake(w->wake_window);
  }
  return 0;
}

async_worker* async_worker_create(llm_config* llm, platform_window* wake_window) {
  async_worker* w = (async_worker*)moyu_alloc(sizeof(*w));
  memset(w, 0, sizeof(*w));
  InitializeCriticalSection(&w->lock);
  InitializeConditionVariable(&w->ready);
  w->next_id = 1;
  w->llm = llm;
  w->wake_window = wake_window;
  w->thread = CreateThread(NULL, 0, worker_main, w, 0, NULL);
  if (!w->thread) {
    DeleteCriticalSection(&w->lock);
    moyu_free(w);
    return NULL;
  }
  return w;
}

void async_worker_destroy(async_worker* w) {
  if (!w) return;
  EnterCriticalSection(&w->lock);
  w->stop = true;
  WakeAllConditionVariable(&w->ready);
  LeaveCriticalSection(&w->lock);
  WaitForSingleObject(w->thread, 35000);
  CloseHandle(w->thread);
  job_free(&w->job);
  async_result_free(&w->result);
  DeleteCriticalSection(&w->lock);
  moyu_free(w);
}

bool async_submit_llm(async_worker* w,
                      const char* purpose,
                      const char* system,
                      const char* user,
                      int timeout_ms,
                      uint64_t* out_id) {
  if (!w || !system || !user) return false;
  EnterCriticalSection(&w->lock);
  if (w->stop || w->has_job || w->working) {
    LeaveCriticalSection(&w->lock);
    return false;
  }
  async_job* j = &w->job;
  j->kind = ASYNC_LLM;
  j->id = w->next_id++;
  j->purpose = moyu_strdup(purpose ? purpose : "reflection");
  j->system = moyu_strdup(system);
  j->user = moyu_strdup(user);
  j->timeout_ms = timeout_ms;
  w->has_job = true;
  if (out_id) *out_id = j->id;
  WakeConditionVariable(&w->ready);
  LeaveCriticalSection(&w->lock);
  return true;
}

bool async_poll(async_worker* w, async_result* out) {
  if (!w || !out) return false;
  EnterCriticalSection(&w->lock);
  if (!w->has_result) {
    LeaveCriticalSection(&w->lock);
    return false;
  }
  *out = w->result;
  memset(&w->result, 0, sizeof(w->result));
  w->has_result = false;
  LeaveCriticalSection(&w->lock);
  return true;
}

bool async_busy(async_worker* w) {
  if (!w) return false;
  EnterCriticalSection(&w->lock);
  bool busy = w->has_job || w->working;
  LeaveCriticalSection(&w->lock);
  return busy;
}

#else
struct async_worker { int unused; };
async_worker* async_worker_create(struct llm_config* l,struct platform_window* w){(void)l;(void)w;return NULL;}
void async_worker_destroy(async_worker* w){(void)w;}
bool async_submit_llm(async_worker* w,const char* p,const char* s,const char* u,int t,uint64_t* i){(void)w;(void)p;(void)s;(void)u;(void)t;(void)i;return false;}
bool async_poll(async_worker* w,async_result* o){(void)w;(void)o;return false;}
void async_result_free(async_result* r){(void)r;}
bool async_busy(async_worker* w){(void)w;return false;}
#endif
