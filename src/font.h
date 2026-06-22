#pragma once
#include "render.h"

#include <stdint.h>

// Render a Unicode text bubble. The bubble expands to fit text and prefers the
// area above (dx, dy_top), flipping below when necessary.
typedef struct {
  int x, y, w, h;
} bubble_rect;

bubble_rect render_bubble(render_ctx* r, const char* text, int dx, int dy_top);
bubble_rect render_info_card(render_ctx* r,
                             const char* title,
                             const char* body,
                             int dx,
                             int dy_top,
                             int max_w);
