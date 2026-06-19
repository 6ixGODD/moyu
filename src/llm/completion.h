#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct llm_config {
    char* base_url;   // e.g. "https://api.deepseek.com/v1"  (owned)
    char* api_key;    // owned
    char* model;      // owned
    int   max_tokens;
    float temperature;
    bool  json_mode;
} llm_config;

void llm_config_init(llm_config* c);
void llm_config_free(llm_config* c);

typedef struct {
    char* text;       // owned; assistant message content
    char* error;      // owned; non-NULL on failure
    int   status;     // HTTP status
} llm_result;

void llm_result_free(llm_result* r);

// messages: alternating "user"/"assistant" messages as plain strings.
// First message should typically be the system prompt; we send it as role=system.
// Subsequent messages alternate user/assistant starting from user.
// The function builds the OpenAI-compatible request body and calls the platform HTTP layer.
llm_result llm_complete(llm_config* cfg, const char* unused_system_separate,
                        const char** messages, size_t n, int timeout_ms);
