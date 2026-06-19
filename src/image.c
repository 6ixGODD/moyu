#include "image.h"

#include "log.h"
#include "mem.h"
#include "platform.h"

#include <stdint.h>
#include <string.h>

static uint16_t rd16(const uint8_t* p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

bool image_load_bmp(const char* path,
                    uint32_t** out_pixels,
                    int* out_w,
                    int* out_h) {
  if (!path || !out_pixels || !out_w || !out_h) return false;
  *out_pixels = NULL;
  *out_w = *out_h = 0;

  size_t flen = 0;
  uint8_t* data = (uint8_t*)platform_read_file(path, &flen);
  if (!data || flen < 54) {
    LOGW("bmp: %s not found or too small", path);
    if (data) moyu_free(data);
    return false;
  }
  if (data[0] != 'B' || data[1] != 'M') {
    LOGW("bmp: %s bad signature", path);
    moyu_free(data);
    return false;
  }

  uint32_t pix_off = rd32(data + 10);
  uint32_t hsize = rd32(data + 14);  // DIB header size
  int32_t w = (int32_t)rd32(data + 18);
  int32_t h = (int32_t)rd32(data + 22);
  uint16_t bpp = rd16(data + 28);
  uint32_t comp = rd32(data + 30);

  if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || comp != 0) {
    LOGW("bmp: %s unsupported (w=%d h=%d bpp=%u comp=%u)", path, w, h, bpp, comp);
    moyu_free(data);
    return false;
  }
  if (pix_off + (size_t)((((w * bpp + 31) / 32) * 4)) * (size_t)(h < 0 ? -h : h) >
      flen) {
    LOGW("bmp: %s truncated", path);
    moyu_free(data);
    return false;
  }

  bool top_down = h < 0;
  int abs_h = top_down ? -h : h;
  int row_bytes = ((w * bpp + 31) / 32) * 4;  // padded to 4

  uint32_t* out = (uint32_t*)moyu_alloc((size_t)w * abs_h * 4);
  const uint8_t* pix = data + pix_off;

  for (int y = 0; y < abs_h; y++) {
    // src row: top-down files start at row 0; bottom-up files start at last row
    int src_y = top_down ? y : (abs_h - 1 - y);
    const uint8_t* row = pix + (size_t)src_y * row_bytes;
    for (int x = 0; x < w; x++) {
      uint8_t b, g, r, a;
      if (bpp == 32) {
        b = row[x * 4 + 0];
        g = row[x * 4 + 1];
        r = row[x * 4 + 2];
        a = row[x * 4 + 3];
        if (a == 0) a = 255;  // many editors write 0 alpha for opaque; treat 0 as opaque unless masked below
      } else {
        b = row[x * 3 + 0];
        g = row[x * 3 + 1];
        r = row[x * 3 + 2];
        a = 255;
      }
      // magenta (or near) = transparent (classic pixel-art mask color)
      if (r >= 240 && g <= 20 && b >= 240) {
        a = 0;
      }
      // store as 0xRRGGBBAA
      out[y * w + x] =
          ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
    }
  }

  moyu_free(data);
  *out_pixels = out;
  *out_w = w;
  *out_h = abs_h;
  return true;
}
