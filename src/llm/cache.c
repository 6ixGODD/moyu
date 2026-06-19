#include "cache.h"
#include "../util/mem.h"
#include "../platform/platform.h"

#include <string.h>

static void unlink_entry(llm_cache* c, llm_cache_entry* e) {
    if (e->prev) e->prev->next = e->next; else c->head = e->next;
    if (e->next) e->next->prev = e->prev; else c->tail = e->prev;
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
