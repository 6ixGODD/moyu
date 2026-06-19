#include "context.h"
#include "../util/mem.h"
#include "../util/hash.h"
#include "../platform/platform.h"

#include <stdio.h>
#include <string.h>

static char* dup_or_null(const char* s) {
    if (!s) return NULL;
    return moyu_strdup(s);
}

void context_store_init(context_store* s, size_t cap) {
    s->cap = cap;
    s->ring = (context_node*)moyu_alloc(cap * sizeof(context_node));
    memset(s->ring, 0, cap * sizeof(context_node));
    s->head = 0;
    s->count = 0;
    s->next_id = 1;
}

void context_store_free(context_store* s) {
    if (!s) return;
    for (size_t i = 0; i < s->cap; i++) {
        if (s->ring[i].input)    moyu_free(s->ring[i].input);
        if (s->ring[i].output)   moyu_free(s->ring[i].output);
        if (s->ring[i].metadata) moyu_free(s->ring[i].metadata);
    }
    moyu_free(s->ring);
    s->ring = NULL;
    s->cap = s->count = s->head = 0;
}

void context_push(context_store* s, context_type type,
                  const char* input, const char* output, const char* metadata) {
    context_node* n = &s->ring[s->head];
    if (n->input)    moyu_free(n->input);
    if (n->output)   moyu_free(n->output);
    if (n->metadata) moyu_free(n->metadata);
    n->id = s->next_id++;
    n->type = type;
    n->input = dup_or_null(input);
    n->output = dup_or_null(output);
    n->metadata = dup_or_null(metadata);
    n->ts_ms = platform_now_ms();
    s->head = (s->head + 1) % s->cap;
    if (s->count < s->cap) s->count++;
}

const context_node* context_last_of(const context_store* s, context_type type) {
    for (size_t i = 0; i < s->count; i++) {
        size_t idx = (s->head + s->cap - 1 - i) % s->cap;
        if (s->ring[idx].type == type) return &s->ring[idx];
    }
    return NULL;
}

const context_node* context_at(const context_store* s, size_t reverse_index) {
    if (reverse_index >= s->count) return NULL;
    size_t idx = (s->head + s->cap - 1 - reverse_index) % s->cap;
    return &s->ring[idx];
}

uint64_t context_hash(const context_store* s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < s->count; i++) {
        const context_node* n = context_at(s, i);
        if (!n) break;
        uint64_t a = n->input  ? hash_fnv1a64_str(n->input)  : 0;
        uint64_t b = n->output ? hash_fnv1a64_str(n->output) : 0;
        h = hash_fnv1a64_combine(h, a);
        h = hash_fnv1a64_combine(h, b);
        h = hash_fnv1a64_combine(h, (uint64_t)n->type);
    }
    return h;
}

// minimal JSON string escape into a growing buffer
static void write_escaped(char** buf, size_t* len, size_t* cap, const char* s) {
    if (!s) return;
    for (const char* p = s; *p; p++) {
        char c = *p;
        const char* esc = NULL;
        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
        }
        if (esc) {
            for (const char* q = esc; *q; q++) {
                if (*len + 1 >= *cap) { *cap *= 2; *buf = (char*)moyu_realloc(*buf, *cap); }
                (*buf)[(*len)++] = *q;
            }
        } else {
            if ((unsigned char)c < 0x20) {
                char hex[8];
                int n = snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)c);
                for (int k = 0; k < n; k++) {
                    if (*len + 1 >= *cap) { *cap *= 2; *buf = (char*)moyu_realloc(*buf, *cap); }
                    (*buf)[(*len)++] = hex[k];
                }
            } else {
                if (*len + 1 >= *cap) { *cap *= 2; *buf = (char*)moyu_realloc(*buf, *cap); }
                (*buf)[(*len)++] = c;
            }
        }
    }
}

static void putc_buf(char** buf, size_t* len, size_t* cap, char c) {
    if (*len + 1 >= *cap) { *cap *= 2; *buf = (char*)moyu_realloc(*buf, *cap); }
    (*buf)[(*len)++] = c;
}

char* context_to_json(const context_store* s, size_t last_n) {
    size_t cap = 1024, len = 0;
    char* buf = (char*)moyu_alloc(cap);
    putc_buf(&buf, &len, &cap, '[');
    size_t n = s->count < last_n ? s->count : last_n;
    for (size_t i = 0; i < n; i++) {
        const context_node* nd = context_at(s, i);
        if (!nd) break;
        if (i > 0) putc_buf(&buf, &len, &cap, ',');
        const char* type_str = "idle";
        switch (nd->type) {
            case CTX_IDLE:        type_str = "idle"; break;
            case CTX_INTERACTION: type_str = "interaction"; break;
            case CTX_TOOL:        type_str = "tool"; break;
            case CTX_REFLECTION:  type_str = "reflection"; break;
        }
        char hdr[128];
        int hn = snprintf(hdr, sizeof(hdr),
            "{\"id\":%llu,\"type\":\"%s\",\"ts\":%llu,\"input\":",
            (unsigned long long)nd->id, type_str, (unsigned long long)nd->ts_ms);
        for (int k = 0; k < hn; k++) putc_buf(&buf, &len, &cap, hdr[k]);
        putc_buf(&buf, &len, &cap, '"');
        write_escaped(&buf, &len, &cap, nd->input);
        putc_buf(&buf, &len, &cap, '"');
        putc_buf(&buf, &len, &cap, ',');
        // output
        if (nd->output) {
            putc_buf(&buf, &len, &cap, '"');
            write_escaped(&buf, &len, &cap, nd->output);
            putc_buf(&buf, &len, &cap, '"');
        } else {
            putc_buf(&buf, &len, &cap, 'n'); putc_buf(&buf, &len, &cap, 'u');
            putc_buf(&buf, &len, &cap, 'l'); putc_buf(&buf, &len, &cap, 'l');
        }
        putc_buf(&buf, &len, &cap, '}');
    }
    putc_buf(&buf, &len, &cap, ']');
    putc_buf(&buf, &len, &cap, 0);
    return buf;
}
