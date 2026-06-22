#pragma once

#include <stdbool.h>

struct moyu_app;
typedef struct chat_ui chat_ui;

chat_ui* chat_ui_create(struct moyu_app* app);
void chat_ui_destroy(chat_ui* ui);
void chat_ui_show(chat_ui* ui);
void chat_ui_append(chat_ui* ui, const char* role, const char* utf8_text);
void chat_ui_context_menu(chat_ui* ui);
void chat_ui_show_quick_chat(chat_ui* ui);
void chat_ui_onboarding(chat_ui* ui);
bool chat_ui_visible(chat_ui* ui);
