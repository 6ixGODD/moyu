#pragma once
#include <stdbool.h>
#include <stdint.h>

// Minimal BMP loader. Supports 24-bit and 32-bit BI_RGB BMPs (uncompressed).
// Output: RGBA8888 pixels (0xRRGGBBAA), top-down row order. magenta
// (0xFF00FF) is treated as transparent -> alpha 0, everything else opaque.
// Caller owns *out_pixels (free with moyu_free). Returns false on any error.
// *out_w/*out_h receive dimensions (always >0 on success).
bool image_load_bmp(const char* path,
                    uint32_t** out_pixels,
                    int* out_w,
                    int* out_h);
