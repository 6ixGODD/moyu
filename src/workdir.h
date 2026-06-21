#pragma once

#include <stdbool.h>

typedef struct moyu_workdir {
  char* root;
  char* soul_path;
  char* memory_path;
  char* db_path;
  char* config_path;
  char* secrets_path;
  char* collections_dir;
  char* drafts_dir;
  char* logs_dir;
} moyu_workdir;

bool workdir_init(moyu_workdir* wd);
void workdir_free(moyu_workdir* wd);
