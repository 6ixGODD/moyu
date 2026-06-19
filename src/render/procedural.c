#include "procedural.h"
#include "sprite.h"
#include "../util/mem.h"

#include <math.h>
#include <string.h>

#define FW 32
#define FH 32

static void put_px(uint32_t* f, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= FW || y >= FH) return;
    f[y * FW + x] = c;
}

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
}

// filled circle (integer)
static void fill_disc(uint32_t* f, int cx, int cy, int r, uint32_t c) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x*x + y*y <= r*r) put_px(f, cx + x, cy + y, c);
        }
    }
}

static void rect_filled(uint32_t* f, int x0, int y0, int x1, int y1, uint32_t c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) put_px(f, x, y, c);
}

static void hline(uint32_t* f, int x0, int x1, int y, uint32_t c) {
    for (int x = x0; x <= x1; x++) put_px(f, x, y, c);
}

static void vline(uint32_t* f, int x, int y0, int y1, uint32_t c) {
    for (int y = y0; y <= y1; y++) put_px(f, x, y, c);
}

// Colors
static const uint32_t C_BODY    = 0;  // set below
static const uint32_t C_DARK    = 0;
static const uint32_t C_WHITE   = 0;
static const uint32_t C_Z       = 0;
static const uint32_t C_BLUSH   = 0;

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) { return rgba(r, g, b, 255); }

static void init_colors(void) {
    *(uint32_t*)&C_BODY  = pack_rgb(120, 170, 230);  // soft blue
    *(uint32_t*)&C_DARK  = pack_rgb(40, 50, 70);
    *(uint32_t*)&C_WHITE = pack_rgb(245, 245, 250);
    *(uint32_t*)&C_Z     = pack_rgb(180, 200, 220);
    *(uint32_t*)&C_BLUSH = pack_rgb(240, 150, 160);
}

// Draw the base blob body with vertical offset (bob).
static void draw_body(uint32_t* f, int yoff, int squish) {
    // squish 0 = normal, +1 wider/shorter, -1 narrower/taller
    int rx = 12 + squish;
    int ry = 12 - squish;
    int cx = 16, cy = 17 + yoff;
    // ellipse
    for (int y = -ry; y <= ry; y++) {
        for (int x = -rx; x <= rx; x++) {
            // (x/rx)^2 + (y/ry)^2 <= 1
            long dx = (long)x * 12 / rx;
            long dy = (long)y * 12 / ry;
            if (dx*dx + dy*dy <= 144) put_px(f, cx + x, cy + y, C_BODY);
        }
    }
    // soft highlight (top-left)
    for (int y = -8; y <= -4; y++) {
        for (int x = -8; x <= -4; x++) {
            if (x*x + y*y <= 36) {
                int px = cx + x, py = cy + y;
                if (px >= 0 && py >= 0 && px < FW && py < FH) {
                    uint32_t c = f[py * FW + px];
                    if ((c >> 24) != 0) {
                        uint8_t r = (c >> 24) & 0xff, g = (c >> 16) & 0xff, b = (c >> 8) & 0xff;
                        r = (uint8_t)((r + 255) / 2);
                        g = (uint8_t)((g + 255) / 2);
                        b = (uint8_t)((b + 255) / 2);
                        put_px(f, px, py, rgba(r, g, b, 255));
                    }
                }
            }
        }
    }
}

// Eyes drawn as small dark rectangles. mode:
//   0 = open (2x2), 1 = blink (3x1 line), 2 = closed (line), 3 = happy (^_^)
// dirx: -1 left, 0 center, 1 right (for observe)
static void draw_eyes(uint32_t* f, int yoff, int mode, int dirx) {
    int lx = 12 + dirx, rx = 20 + dirx, ey = 14 + yoff;
    switch (mode) {
        case 0:  // open
            rect_filled(f, lx - 1, ey - 1, lx, ey, C_DARK);
            rect_filled(f, rx, ey - 1, rx + 1, ey, C_DARK);
            put_px(f, lx - 1, ey - 1, C_WHITE);
            put_px(f, rx, ey - 1, C_WHITE);
            break;
        case 1:  // blink
            hline(f, lx - 1, lx, ey, C_DARK);
            hline(f, rx, rx + 1, ey, C_DARK);
            break;
        case 2:  // closed (sleep)
            hline(f, lx - 1, lx, ey, C_DARK);
            hline(f, rx, rx + 1, ey, C_DARK);
            break;
        case 3: {  // happy ^_^
            put_px(f, lx - 1, ey, C_DARK); put_px(f, lx, ey - 1, C_DARK);
            put_px(f, rx, ey - 1, C_DARK); put_px(f, rx + 1, ey, C_DARK);
            break;
        }
    }
}

static void draw_mouth(uint32_t* f, int yoff, int kind) {
    int mx = 16, my = 20 + yoff;
    switch (kind) {
        case 0: hline(f, mx - 1, mx, my, C_DARK); break;            // neutral
        case 1: put_px(f, mx - 1, my, C_DARK); put_px(f, mx + 1, my, C_DARK);
                put_px(f, mx, my + 1, C_DARK); break;               // small o
        case 2: put_px(f, mx - 1, my + 1, C_DARK); put_px(f, mx, my, C_DARK);
                put_px(f, mx + 1, my + 1, C_DARK); break;           // smile
        case 3: put_px(f, mx - 1, my, C_DARK); put_px(f, mx, my + 1, C_DARK);
                put_px(f, mx + 1, my, C_DARK); break;               // frown
    }
}

static void draw_z(uint32_t* f, int frame) {
    // Z floats up: frame 0 bottom, 3 top
    int x = 22, y = 6 - (frame % 4) / 2;
    (void)frame;
    // crude Z
    hline(f, x, x + 3, y, C_Z);
    put_px(f, x + 2, y + 1, C_Z);
    put_px(f, x + 1, y + 2, C_Z);
    hline(f, x, x + 3, y + 3, C_Z);
}

static void clear_frame(uint32_t* f) {
    for (int i = 0; i < FW * FH; i++) f[i] = 0;
}

static sprite_sheet make_sheet(int frames) {
    sprite_sheet s;
    s.frame_w = FW; s.frame_h = FH; s.frame_count = frames;
    s.pixels = (uint32_t*)moyu_alloc((size_t)FW * FH * frames * 4);
    memset(s.pixels, 0, (size_t)FW * FH * frames * 4);
    return s;
}

sprite_sheet* procedural_build_all(void) {
    init_colors();
    sprite_sheet* arr = (sprite_sheet*)moyu_alloc(sizeof(sprite_sheet) * ANIM_COUNT);

    // IDLE: 4 frames, gentle bob
    {
        sprite_sheet s = make_sheet(4);
        for (int i = 0; i < 4; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            int yoff = (i % 2 == 0) ? 0 : -1;
            draw_body(f, yoff, 0);
            draw_eyes(f, yoff, 0, 0);
            draw_mouth(f, yoff, 0);
        }
        arr[ANIM_IDLE] = s;
    }
    // BLINK: 2 frames
    {
        sprite_sheet s = make_sheet(2);
        for (int i = 0; i < 2; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            draw_body(f, 0, 0);
            draw_eyes(f, 0, i == 0 ? 0 : 1, 0);
            draw_mouth(f, 0, 0);
        }
        arr[ANIM_BLINK] = s;
    }
    // SLEEP: 4 frames, closed eyes, Z floats
    {
        sprite_sheet s = make_sheet(4);
        for (int i = 0; i < 4; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            draw_body(f, 1, 1);
            draw_eyes(f, 1, 2, 0);
            draw_mouth(f, 1, 0);
            draw_z(f, i);
        }
        arr[ANIM_SLEEP] = s;
    }
    // HAPPY: 4 frames, bounce + smile
    {
        sprite_sheet s = make_sheet(4);
        for (int i = 0; i < 4; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            int yoff = (i < 2) ? -1 : 0;
            draw_body(f, yoff, 0);
            draw_eyes(f, yoff, 3, 0);
            draw_mouth(f, yoff, 2);
            // blush
            put_px(f, 10, 18 + yoff, C_BLUSH);
            put_px(f, 21, 18 + yoff, C_BLUSH);
        }
        arr[ANIM_HAPPY] = s;
    }
    // SAD: 4 frames, droop
    {
        sprite_sheet s = make_sheet(4);
        for (int i = 0; i < 4; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            draw_body(f, 1, 1);
            draw_eyes(f, 1, 0, 0);
            draw_mouth(f, 1, 3);
        }
        arr[ANIM_SAD] = s;
    }
    // OBSERVE: 2 frames, eyes shift left/right
    {
        sprite_sheet s = make_sheet(2);
        for (int i = 0; i < 2; i++) {
            uint32_t* f = (uint32_t*)s.pixels + (size_t)i * FW * FH;
            clear_frame(f);
            draw_body(f, 0, 0);
            draw_eyes(f, 0, 0, i == 0 ? -1 : 1);
            draw_mouth(f, 0, 1);
        }
        arr[ANIM_OBSERVE] = s;
    }
    return arr;
}
