#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct t_system_snapshot {
  uint64_t epoch_ms;
  double cpu_percent;
  double memory_percent;
  char host[128];
  char ip[64];
  char local_time[64];
} t_system_snapshot;

typedef struct t_owner_profile {
  char name[96];
  char note[256];
} t_owner_profile;

typedef struct t_path_preview {
  char path[4096];
  char title[192];
  char kind[24];
  char excerpt[4096];
  uint64_t size_bytes;
  bool is_directory;
  bool is_textual;
  bool is_image;
  bool is_video;
} t_path_preview;

bool desktop_os_collect_system_snapshot(t_system_snapshot* out);
char* desktop_os_format_system_snapshot(const t_system_snapshot* s);
void desktop_os_default_owner_profile(t_owner_profile* out);
bool desktop_os_preview_path(const char* path, t_path_preview* out);
