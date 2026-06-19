#include "sprite.h"
#include "../util/mem.h"

void sprite_sheet_free(sprite_sheet* s) {
    if (!s) return;
    if (s->pixels) moyu_free(s->pixels);
    s->pixels = NULL;
}
