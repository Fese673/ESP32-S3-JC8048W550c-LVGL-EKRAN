import struct, math

W = 96
H = 96

def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)

def dist(ax, ay, bx, by):
    return ((ax-bx)**2 + (ay-by)**2) ** 0.5

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def fill_circle(pixels, cx, cy, r, cr, cg, cb, aa=1.5):
    for y in range(H):
        for x in range(W):
            d = dist(x, y, cx, cy)
            if d <= r - aa:
                pixels[y][x] = (cr, cg, cb, 255)
            elif d <= r:
                a = int(255 * (1.0 - (d - (r - aa)) / aa))
                if a > pixels[y][x][3]:
                    pixels[y][x] = (cr, cg, cb, a)

def circle(pixels, cx, cy, r, cr, cg, cb, feather=1.5):
    for y in range(H):
        for x in range(W):
            d = abs(dist(x, y, cx, cy) - r)
            if d <= feather:
                a = int(clamp(255 * (1.0 - d/feather), 0, 255))
                if a > pixels[y][x][3]:
                    pixels[y][x] = (cr, cg, cb, a)

def line(pixels, x1, y1, x2, y2, cr, cg, cb, width=3.0):
    for y in range(H):
        for x in range(W):
            dx = x2 - x1
            dy = y2 - y1
            if dx == 0 and dy == 0:
                d = dist(x, y, x1, y1)
            else:
                t = ((x-x1)*dx + (y-y1)*dy) / (dx*dx + dy*dy)
                t = clamp(t, 0.0, 1.0)
                px = x1 + t*dx
                py = y1 + t*dy
                d = dist(x, y, px, py)
            if d <= width/2:
                a = int(255 * clamp(1.0 - (d - width/2 + 1.0), 0.0, 1.0))
                if a > pixels[y][x][3]:
                    pixels[y][x] = (cr, cg, cb, a)

def cloud(pixels, cx, cy, scale, cr, cg, cb):
    s = scale
    fill_circle(pixels, cx, cy, int(28*s), cr, cg, cb)
    fill_circle(pixels, cx-28*s, cy+4, int(22*s), cr, cg, cb)
    fill_circle(pixels, cx+28*s, cy+4, int(22*s), cr, cg, cb)
    fill_circle(pixels, cx-14*s, cy-16, int(18*s), cr, cg, cb)
    fill_circle(pixels, cx+14*s, cy-16, int(18*s), cr, cg, cb)

def generate_icon(name, draw_func):
    pixels = [[(0,0,0,0) for _ in range(W)] for _ in range(H)]
    draw_func(pixels)

    buf = bytearray()
    alpha_buf = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b, a = pixels[y][x]
            buf += struct.pack('<H', rgb565(r, g, b))
            alpha_buf += struct.pack('B', a)

    c_name = name.replace('-', '_')
    arr = f"img_wi_{c_name}_map"
    dsc = f"img_wi_{c_name}"

    lines = [
        '#ifdef __has_include',
        '    #if __has_include("lvgl.h")',
        '        #ifndef LV_LVGL_H_INCLUDE_SIMPLE',
        '            #define LV_LVGL_H_INCLUDE_SIMPLE',
        '        #endif',
        '    #endif',
        '#endif',
        '',
        '#if defined(LV_LVGL_H_INCLUDE_SIMPLE)',
        '#include "lvgl.h"',
        '#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)',
        '#include <lvgl.h>',
        '#elif defined(LV_BUILD_TEST)',
        '#include "../lvgl.h"',
        '#else',
        '#include "lvgl/lvgl.h"',
        '#endif',
        '',
        f'#ifndef LV_ATTRIBUTE_MEM_ALIGN',
        f'#define LV_ATTRIBUTE_MEM_ALIGN',
        f'#endif',
        '',
        f'#ifndef LV_ATTRIBUTE_IMG_WI_{c_name.upper()}',
        f'#define LV_ATTRIBUTE_IMG_WI_{c_name.upper()}',
        f'#endif',
        '',
        f'static const',
        f'LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_WI_{c_name.upper()}',
        f'uint8_t {arr}[] = {{',
    ]

    all_data = bytes(buf + alpha_buf)
    for i in range(0, len(all_data), 32):
        chunk = all_data[i:i+32]
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',')

    lines += [
        '};',
        '',
        f'const lv_image_dsc_t {dsc} = {{',
        '  .header = {',
        '    .magic = LV_IMAGE_HEADER_MAGIC,',
        '    .cf = LV_COLOR_FORMAT_RGB565A8,',
        '    .flags = 0,',
        f'    .w = {W},',
        f'    .h = {H},',
        f'    .stride = {W*2},',
        '    .reserved_2 = 0,',
        '  },',
        f'  .data_size = sizeof({arr}),',
        f'  .data = {arr},',
        '  .reserved = NULL,',
        '};',
    ]
    return '\n'.join(lines)

def img(name, draw_func):
    with open(f"wi_{name}.c", 'w', encoding='utf-8') as f:
        f.write(generate_icon(name, draw_func))
    print(f"  wi_{name}.c")

# === ICON DRAWING FUNCTIONS (2x scale from 48x48) ===

def draw_sunny(p):
    fill_circle(p, 48, 48, 18, 255, 221, 0)
    circle(p, 48, 48, 18, 255, 200, 0, feather=2)

def draw_partly_cloudy(p):
    fill_circle(p, 32, 40, 13, 255, 221, 0)
    circle(p, 32, 40, 13, 255, 200, 0, feather=1.5)
    cloud(p, 52, 54, 0.70, 240, 240, 245)

def draw_cloudy(p):
    cloud(p, 44, 48, 0.85, 230, 230, 235)

def draw_fog(p):
    for y in [30, 48, 66]:
        line(p, 20, y, 76, y, 200, 200, 210, width=5)
        line(p, 24, y+2, 72, y+2, 220, 220, 230, width=2.5)

def draw_windy(p):
    line(p, 16, 34, 80, 34, 210, 210, 215, width=4)
    line(p, 24, 50, 76, 50, 200, 200, 210, width=3)
    line(p, 28, 66, 72, 66, 190, 190, 200, width=2.5)
    fill_circle(p, 24, 34, 4, 215, 195, 165)
    fill_circle(p, 72, 50, 4, 215, 195, 165)
    fill_circle(p, 56, 66, 3, 215, 195, 165)

def draw_dry(p):
    fill_circle(p, 48, 40, 18, 255, 221, 0)
    circle(p, 48, 40, 18, 255, 200, 0, feather=1.5)
    line(p, 32, 68, 64, 68, 255, 180, 50, width=2.5)
    line(p, 38, 76, 58, 76, 255, 160, 30, width=2)

def draw_frost(p):
    cx, cy = 48, 48
    for angle in range(0, 360, 60):
        rad = math.radians(angle)
        ex = cx + int(28 * math.cos(rad))
        ey = cy + int(28 * math.sin(rad))
        line(p, cx, cy, ex, ey, 180, 230, 255, width=3.5)
        mid_x = cx + int(16 * math.cos(rad))
        mid_y = cy + int(16 * math.sin(rad))
        perp = rad + math.pi/2
        line(p, mid_x + int(8*math.cos(perp)), mid_y + int(8*math.sin(perp)),
             mid_x - int(8*math.cos(perp)), mid_y - int(8*math.sin(perp)),
             180, 230, 255, width=2.5)
    fill_circle(p, cx, cy, 5, 180, 230, 255)

def draw_drizzle(p):
    cloud(p, 48, 36, 0.8, 220, 220, 225)
    fill_circle(p, 36, 64, 3, 120, 180, 255)
    fill_circle(p, 52, 72, 3, 120, 180, 255)
    fill_circle(p, 64, 66, 3, 120, 180, 255)

def draw_rain(p):
    cloud(p, 48, 36, 0.85, 210, 210, 220)
    line(p, 28, 60, 24, 84, 100, 160, 255, width=3.5)
    line(p, 44, 64, 40, 88, 100, 160, 255, width=3.5)
    line(p, 60, 60, 56, 84, 100, 160, 255, width=3.5)
    line(p, 76, 64, 72, 88, 100, 160, 255, width=3.5)

def draw_snow(p):
    cloud(p, 48, 36, 0.85, 230, 230, 240)
    fill_circle(p, 28, 64, 5, 180, 230, 255)
    fill_circle(p, 48, 76, 5, 180, 230, 255)
    fill_circle(p, 68, 64, 5, 180, 230, 255)
    line(p, 24, 60, 32, 68, 180, 230, 255, width=2.5)
    line(p, 44, 72, 52, 80, 180, 230, 255, width=2.5)
    line(p, 64, 60, 72, 68, 180, 230, 255, width=2.5)

def draw_sleet(p):
    cloud(p, 48, 36, 0.85, 215, 215, 225)
    line(p, 28, 60, 24, 84, 100, 160, 255, width=3.5)
    fill_circle(p, 60, 68, 5, 180, 230, 255)
    line(p, 56, 64, 64, 72, 180, 230, 255, width=2.5)
    fill_circle(p, 76, 72, 4, 180, 230, 255)

def draw_storm(p):
    cloud(p, 48, 36, 0.85, 130, 130, 140)
    line(p, 56, 48, 44, 64, 255, 200, 0, width=5)
    line(p, 44, 64, 52, 64, 255, 200, 0, width=5)
    line(p, 52, 64, 40, 84, 255, 200, 0, width=5)
    line(p, 40, 84, 48, 84, 255, 200, 0, width=5)
    line(p, 56, 48, 44, 64, 255, 240, 150, width=2)
    line(p, 52, 64, 40, 84, 255, 240, 150, width=2)

def draw_na(p):
    fill_circle(p, 48, 30, 16, 200, 200, 210)
    line(p, 48, 48, 48, 60, 200, 200, 210, width=5)
    line(p, 48, 60, 48, 76, 200, 200, 210, width=5)
    fill_circle(p, 48, 80, 4, 200, 200, 210)

# === GENERATE ALL ===
print("Generating weather icons (96x96):")
for name, fn in [
    ("sunny", draw_sunny),
    ("partly_cloudy", draw_partly_cloudy),
    ("cloudy", draw_cloudy),
    ("fog", draw_fog),
    ("windy", draw_windy),
    ("dry", draw_dry),
    ("frost", draw_frost),
    ("drizzle", draw_drizzle),
    ("rain", draw_rain),
    ("snow", draw_snow),
    ("sleet", draw_sleet),
    ("storm", draw_storm),
    ("na", draw_na),
]:
    img(name, fn)
print("Done!")
