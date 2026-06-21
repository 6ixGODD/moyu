#include "llm.h"

#include "cJSON.h"
#include "log.h"
#include "mem.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

void llm_config_init(llm_config* c) {
  memset(c, 0, sizeof(*c));
  c->base_url = moyu_strdup("https://open.bigmodel.cn/api/paas/v4");
  c->api_key = moyu_strdup("");
  c->model = moyu_strdup("glm-4.7-flash");
  c->max_tokens = 256;
  c->temperature = 0.8f;
  c->json_mode = false;
}

void llm_config_free(llm_config* c) {
  if (!c) return;
  if (c->base_url) moyu_free(c->base_url);
  if (c->api_key) moyu_free(c->api_key);
  if (c->model) moyu_free(c->model);
  memset(c, 0, sizeof(*c));
}

void llm_result_free(llm_result* r) {
  if (!r) return;
  if (r->text) moyu_free(r->text);
  if (r->error) moyu_free(r->error);
  r->text = NULL;
  r->error = NULL;
  r->status = 0;
}

// Build request body:
// { "model": "...", "messages": [{role,content}, ...],
//   "max_tokens": N, "temperature": T, "stream": false,
//   optional: "response_format": {"type":"json_object"} }
static char* build_body(const llm_config* cfg,
                        const char** messages,
                        size_t n) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", cfg->model);
  cJSON* msgs = cJSON_CreateArray();
  for (size_t i = 0; i < n; i++) {
    cJSON* m = cJSON_CreateObject();
    const char* role =
        (i == 0) ? "system" : ((i % 2 == 1) ? "user" : "assistant");
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", messages[i] ? messages[i] : "");
    cJSON_AddItemToArray(msgs, m);
  }
  cJSON_AddItemToObject(root, "messages", msgs);
  cJSON_AddNumberToObject(root, "max_tokens", cfg->max_tokens);
  cJSON_AddNumberToObject(root, "temperature", cfg->temperature);
  cJSON_AddBoolToObject(root, "stream", 0);
  if (cfg->json_mode) {
    cJSON* rf = cJSON_CreateObject();
    cJSON_AddStringToObject(rf, "type", "json_object");
    cJSON_AddItemToObject(root, "response_format", rf);
  }
  char* body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return body;
}

llm_result llm_complete(llm_config* cfg,
                        const char* unused,
                        const char** messages,
                        size_t n,
                        int timeout_ms) {
  llm_result res = {0};
  (void)unused;
  if (!cfg || !messages || n == 0) {
    res.error = moyu_strdup("null cfg/messages");
    return res;
  }
  if (!cfg->api_key || !cfg->api_key[0]) {
    res.error = moyu_strdup("api_key empty");
    return res;
  }

  char* body = build_body(cfg, messages, n);
  if (!body) {
    res.error = moyu_strdup("body build failed");
    return res;
  }

  // URL: base_url + "/chat/completions"
  size_t bl = strlen(cfg->base_url);
  size_t ul = bl + 32;
  char* url = (char*)moyu_alloc(ul);
  snprintf(url,
           ul,
           "%s%s/chat/completions",
           cfg->base_url,
           (cfg->base_url[bl - 1] == '/') ? "" : "");

  platform_http_resp r =
      platform_http_post_json(url, cfg->api_key, body, timeout_ms);
  moyu_free(body);
  moyu_free(url);
  res.status = r.status;
  LOGI("LLM HTTP: status=%d body_len=%zu err=%s",
       r.status,
       r.body_len,
       r.err ? r.err : "(null)");
  if (r.err) {
    res.error = moyu_strdup(r.err);
    platform_http_resp_free(&r);
    return res;
  }
  if (r.status != 200) {
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "HTTP %d: %.*s",
             r.status,
             (int)(r.body_len < 200 ? r.body_len : 200),
             r.body ? r.body : "");
    res.error = moyu_strdup(buf);
    platform_http_resp_free(&r);
    return res;
  }
  // Parse JSON: choices[0].message.content
  cJSON* root = cJSON_ParseWithLength(r.body, r.body_len);
  if (!root) {
    res.error = moyu_strdup("invalid JSON in response");
    platform_http_resp_free(&r);
    return res;
  }
  cJSON* choices = cJSON_GetObjectItem(root, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    cJSON* err = cJSON_GetObjectItem(root, "error");
    if (err) {
      cJSON* msg = cJSON_GetObjectItem(err, "message");
      res.error = moyu_strdup(msg && cJSON_IsString(msg) ? msg->valuestring
                                                         : "unknown error");
    } else {
      res.error = moyu_strdup("no choices in response");
    }
    cJSON_Delete(root);
    platform_http_resp_free(&r);
    return res;
  }
  cJSON* first = cJSON_GetArrayItem(choices, 0);
  cJSON* message = cJSON_GetObjectItem(first, "message");
  cJSON* content = message ? cJSON_GetObjectItem(message, "content") : NULL;
  if (content && cJSON_IsString(content) && content->valuestring[0]) {
    res.text = moyu_strdup(content->valuestring);
  } else {
    // Reasoning models (e.g. GLM-4.7-flash) may emit the answer in
    // reasoning_content when they run out of tokens before the final answer.
    // Fall back to it rather than returning nothing.
    cJSON* reasoning =
        message ? cJSON_GetObjectItem(message, "reasoning_content") : NULL;
    if (reasoning && cJSON_IsString(reasoning) && reasoning->valuestring[0]) {
      res.text = moyu_strdup(reasoning->valuestring);
      LOGW("LLM: empty content, used reasoning_content fallback");
    } else {
      res.error = moyu_strdup("no content in message");
    }
  }
  cJSON_Delete(root);
  platform_http_resp_free(&r);
  return res;
}
#include "mem.h"
#include "platform.h"

#include <string.h>

static void unlink_entry(llm_cache* c, llm_cache_entry* e) {
  if (e->prev)
    e->prev->next = e->next;
  else
    c->head = e->next;
  if (e->next)
    e->next->prev = e->prev;
  else
    c->tail = e->prev;
  e->prev = e->next = NULL;
}

static void push_front(llm_cache* c, llm_cache_entry* e) {
  e->prev = NULL;
  e->next = c->head;
  if (c->head) c->head->prev = e;
  c->head = e;
  if (!c->tail) c->tail = e;
}

void llm_cache_init(llm_cache* c, size_t cap) {
  c->head = c->tail = NULL;
  c->count = 0;
  c->cap = cap;
}

void llm_cache_free(llm_cache* c) {
  llm_cache_entry* e = c->head;
  while (e) {
    llm_cache_entry* n = e->next;
    if (e->value) moyu_free(e->value);
    moyu_free(e);
    e = n;
  }
  c->head = c->tail = NULL;
  c->count = 0;
}

char* llm_cache_get(llm_cache* c, uint64_t key) {
  for (llm_cache_entry* e = c->head; e; e = e->next) {
    if (e->key == key) {
      // move to front
      if (e != c->head) {
        unlink_entry(c, e);
        push_front(c, e);
      }
      return e->value ? moyu_strdup(e->value) : NULL;
    }
  }
  return NULL;
}

void llm_cache_put(llm_cache* c, uint64_t key, const char* value) {
  for (llm_cache_entry* e = c->head; e; e = e->next) {
    if (e->key == key) {
      if (e->value) moyu_free(e->value);
      e->value = value ? moyu_strdup(value) : NULL;
      e->ts_ms = platform_now_ms();
      if (e != c->head) {
        unlink_entry(c, e);
        push_front(c, e);
      }
      return;
    }
  }
  llm_cache_entry* e = (llm_cache_entry*)moyu_alloc(sizeof(*e));
  e->key = key;
  e->value = value ? moyu_strdup(value) : NULL;
  e->ts_ms = platform_now_ms();
  e->prev = e->next = NULL;
  push_front(c, e);
  c->count++;
  if (c->count > c->cap) {
    llm_cache_entry* t = c->tail;
    unlink_entry(c, t);
    if (t->value) moyu_free(t->value);
    moyu_free(t);
    c->count--;
  }
}
