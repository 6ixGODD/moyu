#pragma once
#include "sprite.h"

// The builtin procedural skin is implemented in procedural.c via skin_init_default.
// (No build_all anymore — the skin model replaced the per-anim sheet array.)
void skin_init_default(skin* sk);
bool skin_init_named(skin* sk, const char* name);
