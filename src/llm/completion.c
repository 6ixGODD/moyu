#include "completion.h"
#include "../platform/platform.h"
#include "../util/mem.h"
#include "../util/log.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

void llm_config_init(llm_config* c) {
    memset(c, 0, sizeof(*c));
    c->base_url = moyu_strdup("https://api.deepseek.com/v1");
    c->api_key  = moyu_strdup("");
    c->model    = moyu_strdup("deepseek-chat");
    c->max_tokens = 256;
    c->temperature = 0.8f;
    c->json_mode = false;
}

void llm_config_free(llm_config* c) {
    if (!c) return;
    if (c->base_url) moyu_free(c->base_url);
    if (c->api_key)  moyu_free(c->api_key);
    if (c->model)    moyu_free(c->model);
    memset(c, 0, sizeof(*c));
}

void llm_result_free(llm_result* r) {
    if (!r) return;
    if (r->text)  moyu_free(r->text);
    if (r->error) moyu_free(r->error);
    r->text = NULL; r->error = NULL; r->status = 0;
}

// Build request body:
// { "model": "...", "messages": [{role,content}, ...],
//   "max_tokens": N, "temperature": T, "stream": false,
//   optional: "response_format": {"type":"json_object"} }
static char* build_body(const llm_config* cfg, const char** messages, size_t n) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON* msgs = cJSON_CreateArray();
    for (size_t i = 0; i < n; i++) {
        cJSON* m = cJSON_CreateObject();
        const char* role = (i == 0) ? "system" : ((i % 2 == 1) ? "user" : "assistant");
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

llm_result llm_complete(llm_config* cfg, const char* unused,
                        const char** messages, size_t n, int timeout_ms) {
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
    snprintf(url, ul, "%s%s/chat/completions", cfg->base_url,
             (cfg->base_url[bl - 1] == '/') ? "" : "");

    platform_http_resp r = platform_http_post_json(url, cfg->api_key, body, timeout_ms);
    moyu_free(body);
    moyu_free(url);
    res.status = r.status;
    LOGI("LLM HTTP: status=%d body_len=%zu err=%s", r.status, r.body_len, r.err ? r.err : "(null)");
    if (r.body && r.body_len > 0) {
        LOGI("LLM HTTP body[:300]: %.300s", r.body);
    }
    if (r.err) {
        res.error = moyu_strdup(r.err);
        platform_http_resp_free(&r);
        return res;
    }
    if (r.status != 200) {
        char buf[256];
        snprintf(buf, sizeof(buf), "HTTP %d: %.*s", r.status,
                 (int)(r.body_len < 200 ? r.body_len : 200), r.body ? r.body : "");
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
            res.error = moyu_strdup(msg && cJSON_IsString(msg) ? msg->valuestring : "unknown error");
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
    if (content && cJSON_IsString(content)) {
        res.text = moyu_strdup(content->valuestring);
    } else {
        res.error = moyu_strdup("no content in message");
    }
    cJSON_Delete(root);
    platform_http_resp_free(&r);
    return res;
}
