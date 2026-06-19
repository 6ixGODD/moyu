#pragma once
#include "render.h"

#include <stdint.h>

// Render a small text bubble. The bubble expands to fit text, rendered above (dx,dy_top).
// Returns the bubble rectangle (x, y, w, h) actually drawn. Text is ASCII only.
typedef struct {
  int x, y, w, h;
} bubble_rect;

bubble_rect render_bubble(render_ctx* r, const char* text, int dx, int dy_top);
