#pragma once

#include <stdbool.h>

struct moyu_app;
void builtin_register_persistent(struct moyu_app* app);
bool builtin_collection_add_note(struct moyu_app* app,
                                 const char* title,
                                 const char* body,
                                 const char* source);
