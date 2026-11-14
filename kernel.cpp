// ============================================================
//  iMimi OS — PC Edition (MinimalZero kernel)
//  Desktop, windows, Files, Trash, Calculator, Clicker, Settings
// ============================================================

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed int          s32;

static inline void outb(u16 p, u8 v)  { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }

// ---- freestanding runtime (компилятор может сам вызвать эти функции) ----
extern "C" {
void* memset(void* d, int c, unsigned int n) { u8* p = (u8*)d; while (n--) *p++ = (u8)c; return d; }
void* memcpy(void* d, const void* s, unsigned int n) { u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d; }
void* memmove(void* d, const void* s, unsigned int n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
}

struct multiboot_info {
    u32 flags; u32 mem_lower, mem_upper; u32 boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4]; u32 mmap_length, mmap_addr; u32 drives_length, drives_addr;
    u32 config_table, boot_loader_name, apm_table; u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr; u32 framebuffer_pitch; u32 framebuffer_width; u32 framebuffer_height;
    u8  framebuffer_bpp; u8  framebuffer_type;
} __attribute__((packed));

#include "images.h"

// ------------------------------------------------------------
//  Время (через TSC, откалиброванный по RTC)
// ------------------------------------------------------------
static u32 tsc16_per_ms = 100000;   // тики TSC/16 в миллисекунду

static inline u32 tnow() {          // TSC >> 4 (младшие 32 бита)
    u32 lo, hi; asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (hi << 28) | (lo >> 4);
}
static inline u32 elapsed_ms(u32 since) { return (tnow() - since) / tsc16_per_ms; }
static inline u32 ms_now() { return tnow() / tsc16_per_ms; }

static u8 cmos_read(u8 reg) { outb(0x70, reg); return inb(0x71); }
static void cmos_write(u8 reg, u8 val) { outb(0x70, (reg | 0x80)); outb(0x71, val); outb(0x70, 0); }

// ------------------------------------------------------------
//  Строки
// ------------------------------------------------------------
static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void scopy(char* d, const char* s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static bool seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int itoa_(int v, char* buf) {
    char t[16]; int n = 0, i = 0; bool neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = '0' + (u % 10); u /= 10; }
    if (neg) buf[i++] = '-';
    while (n) buf[i++] = t[--n];
    buf[i] = 0; return i;
}
static int sappend(char* d, int pos, const char* s, int max) {
    while (*s && pos < max - 1) d[pos++] = *s++;
    d[pos] = 0; return pos;
}

// ------------------------------------------------------------
//  Графика (двойная буферизация)
// ------------------------------------------------------------
#define MAXW 1280
#define MAXH 1024

namespace gfx {
    static u8* fb = nullptr;
    static u32 W = 1024, H = 768, pitch = 0;
    static u8 bpp = 32;
    static bool ok = false;
    static u32 back[MAXW * MAXH];

    void init(multiboot_info* mb) {
        if ((mb->flags & (1u << 12)) && mb->framebuffer_addr) {
            fb = (u8*)(u32)mb->framebuffer_addr;
            W = mb->framebuffer_width; H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch; bpp = mb->framebuffer_bpp;
            if (W > MAXW) W = MAXW;
            if (H > MAXH) H = MAXH;
            ok = (bpp == 32 || bpp == 24) && W >= 640 && H >= 480;
        }
    }
    static inline void put_pixel(int x, int y, u32 c) {
        if ((u32)x >= W || (u32)y >= H) return;
        back[y * W + x] = c;
    }
    static inline u32 blend(u32 d, u32 s, u32 a) {            // a: 0..256
        u32 rb = (((d & 0xFF00FF) * (256 - a) + (s & 0xFF00FF) * a) >> 8) & 0xFF00FF;
        u32 g  = (((d & 0x00FF00) * (256 - a) + (s & 0x00FF00) * a) >> 8) & 0x00FF00;
        return rb | g;
    }
    static inline u32 lerp(u32 c1, u32 c2, int t, int tmax) {
        int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
        int r2 = (c2 >> 16) & 255, g2 = (c2 >> 8) & 255, b2 = c2 & 255;
        int r = r1 + (r2 - r1) * t / tmax, g = g1 + (g2 - g1) * t / tmax, b = b1 + (b2 - b1) * t / tmax;
        return (r << 16) | (g << 8) | b;
    }
    static bool clip(int& x, int& y, int& w, int& h) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int)W) w = (int)W - x;
        if (y + h > (int)H) h = (int)H - y;
        return w > 0 && h > 0;
    }
    void fill_rect(int x, int y, int w, int h, u32 c) {
        if (!clip(x, y, w, h)) return;
        for (int j = 0; j < h; j++) { u32* row = &back[(y + j) * W + x]; for (int i = 0; i < w; i++) row[i] = c; }
    }
    void fill_rect_a(int x, int y, int w, int h, u32 c, u32 a) {
        if (!clip(x, y, w, h)) return;
        for (int j = 0; j < h; j++) { u32* row = &back[(y + j) * W + x]; for (int i = 0; i < w; i++) row[i] = blend(row[i], c, a); }
    }
    void fill_gradient(int x, int y, int w, int h, u32 c1, u32 c2) {
        for (int j = 0; j < h; j++) fill_rect(x, y + j, w, 1, lerp(c1, c2, j, h > 1 ? h - 1 : 1));
    }
    void rect_outline(int x, int y, int w, int h, u32 c) {
        fill_rect(x, y, w, 1, c); fill_rect(x, y + h - 1, w, 1, c);
        fill_rect(x, y, 1, h, c); fill_rect(x + w - 1, y, 1, h, c);
    }
    static int isqrt_(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
    static int corner_inset(int r, int dy) {                   // dy: строка от края (0..r-1)
        int d = 2 * (r - dy) - 1;
        int v = 4 * r * r - d * d; if (v < 0) v = 0;
        return r - isqrt_(v) / 2;
    }
    void fill_round(int x, int y, int w, int h, int r, u32 c, u32 a = 256) {
        if (r * 2 > h) r = h / 2;
        if (r * 2 > w) r = w / 2;
        for (int j = 0; j < h; j++) {
            int dy = j < h - 1 - j ? j : h - 1 - j;
            int ins = dy < r ? corner_inset(r, dy) : 0;
            if (a >= 256) fill_rect(x + ins, y + j, w - 2 * ins, 1, c);
            else fill_rect_a(x + ins, y + j, w - 2 * ins, 1, c, a);
        }
    }
    void fill_circle(int cx, int cy, int r, u32 c, u32 a = 256) {
        for (int j = -r; j <= r; j++) {
            int half = isqrt_(r * r - j * j);
            if (a >= 256) fill_rect(cx - half, cy + j, half * 2 + 1, 1, c);
            else fill_rect_a(cx - half, cy + j, half * 2 + 1, 1, c, a);
        }
    }
    static inline u32 rgb565_to_888(u16 c) {
        u32 r = ((c >> 11) & 0x1F) * 255 / 31, g = ((c >> 5) & 0x3F) * 255 / 63, b = (c & 0x1F) * 255 / 31;
        return (r << 16) | (g << 8) | b;
    }
    // RLE-картинка с масштабированием (без дырок при увеличении)
    void draw_image(int ox, int oy, int dw, int dh, const u16* rle, u32 size, int iw, int ih) {
        u32 idx = 0; int x = 0, y = 0;
        while (idx < size && y < ih) {
            u16 hd = rle[idx++];
            u32 len = hd & 0x7FFF; bool run = (hd & 0x8000) != 0;
            u32 col = 0; if (run) col = rgb565_to_888(rle[idx++]);
            for (u32 i = 0; i < len && y < ih; i++) {
                if (!run) col = rgb565_to_888(rle[idx++]);
                int x0 = ox + x * dw / iw, x1 = ox + (x + 1) * dw / iw;
                int y0 = oy + y * dh / ih, y1 = oy + (y + 1) * dh / ih;
                fill_rect(x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1, col);
                if (++x >= iw) { x = 0; y++; }
            }
        }
    }
    void swap() {
        if (!fb) return;
        if (bpp == 32) {
            u32 p32 = pitch / 4;
            for (u32 y = 0; y < H; y++) {
                u32* d = (u32*)fb + y * p32; u32* s = &back[y * W];
                for (u32 x = 0; x < W; x++) d[x] = s[x];
            }
        } else {
            for (u32 y = 0; y < H; y++) {
                u8* d = fb + y * pitch; u32* s = &back[y * W];
                for (u32 x = 0; x < W; x++) { u32 c = s[x]; d[x*3] = c & 255; d[x*3+1] = (c >> 8) & 255; d[x*3+2] = (c >> 16) & 255; }
            }
        }
    }
}

static const u8 font8x8[95][8] = {
    {0,0,0,0,0,0,0,0},{0x18,0x18,0x18,0x18,0,0,0x18,0},{0x6C,0x6C,0x6C,0,0,0,0,0},{0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0},
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0},{0,0xC6,0xCC,0x18,0x30,0x66,0xC6,0},{0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0},
    {0x18,0x18,0x30,0,0,0,0,0},{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0},{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0},
    {0,0x66,0x3C,0xFF,0x3C,0x66,0,0},{0,0x18,0x18,0x7E,0x18,0x18,0,0},{0,0,0,0,0,0x18,0x18,0x30},{0,0,0,0x7E,0,0,0,0},
    {0,0,0,0,0,0x18,0x18,0},{0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0},{0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0},{0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0},{0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0},
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0},{0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0},{0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0},
    {0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0},{0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0},{0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0},
    {0,0x18,0x18,0,0,0x18,0x18,0},{0,0x18,0x18,0,0,0x18,0x18,0x30},{0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0},
    {0,0,0x7E,0,0x7E,0,0,0},{0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0},{0x7C,0xC6,0x06,0x1C,0x18,0,0x18,0},
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0},{0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0},{0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0},
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0},{0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0},{0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0},
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0},{0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0},{0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0},{0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0},{0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0},
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0},{0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0},{0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0},
    {0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0},{0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0},{0x78,0xCC,0xCC,0xCC,0xDC,0x78,0x1C,0},
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0},{0x78,0xCC,0x60,0x30,0x18,0xCC,0x78,0},{0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0},
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0},{0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0},{0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0},
    {0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0},{0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0},{0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0},
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0},{0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0},{0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0},
    {0x10,0x38,0x6C,0xC6,0,0,0,0},{0,0,0,0,0,0,0,0xFF},{0x30,0x18,0x0C,0,0,0,0,0},{0,0,0x78,0x0C,0x7C,0xCC,0x76,0},
    {0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0},{0,0,0x78,0xCC,0xC0,0xCC,0x78,0},{0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0},
    {0,0,0x78,0xCC,0xFC,0xC0,0x78,0},{0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0},{0,0,0x76,0xCC,0xCC,0x7C,0x0C,0xF8},
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0},{0x18,0,0x38,0x18,0x18,0x18,0x3C,0},{0x06,0,0x06,0x06,0x06,0x66,0x66,0x3C},
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0},{0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0},{0,0,0xEC,0xFE,0xD6,0xC6,0xC6,0},
    {0,0,0xDC,0x66,0x66,0x66,0x66,0},{0,0,0x78,0xCC,0xCC,0xCC,0x78,0},{0,0,0xDC,0x66,0x66,0x7C,0x60,0xF0},
    {0,0,0x76,0xCC,0xCC,0x7C,0x0C,0x1E},{0,0,0xDC,0x76,0x66,0x60,0xF0,0},{0,0,0x7C,0xC0,0x78,0x0C,0xF8,0},
    {0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0},{0,0,0xCC,0xCC,0xCC,0xCC,0x76,0},{0,0,0xCC,0xCC,0xCC,0x78,0x30,0},
    {0,0,0xC6,0xC6,0xD6,0xFE,0x6C,0},{0,0,0xC6,0x6C,0x38,0x6C,0xC6,0},{0,0,0xCC,0xCC,0xCC,0x7C,0x0C,0xF8},
    {0,0,0xFC,0x98,0x30,0x64,0xFC,0},{0x1C,0x18,0x18,0xE0,0x18,0x18,0x1C,0},{0x18,0x18,0x18,0,0x18,0x18,0x18,0},
    {0xE0,0x18,0x18,0x1C,0x18,0x18,0xE0,0},{0x76,0xDC,0,0,0,0,0,0}
};

// ------------------------------------------------------------
//  Текст
// ------------------------------------------------------------
static void draw_char(char c, int x, int y, u32 color, int sc = 1) {
    if (c < 32 || c > 126) c = '?';
    const u8* g = font8x8[c - 32];
    for (int r = 0; r < 8; r++) for (int cl = 0; cl < 8; cl++)
        if (g[r] & (1 << (7 - cl))) {
            if (sc == 1) gfx::put_pixel(x + cl, y + r, color);
            else gfx::fill_rect(x + cl * sc, y + r * sc, sc, sc, color);
        }
}
static void draw_text(const char* s, int x, int y, u32 color, int sc = 1) { while (*s) { draw_char(*s++, x, y, color, sc); x += 8 * sc; } }
static void draw_text_n(const char* s, int x, int y, u32 color, int maxc) { int n = 0; while (*s && n < maxc) { draw_char(*s++, x, y, color); x += 8; n++; } }
static int  text_w(const char* s, int sc = 1) { return slen(s) * 8 * sc; }
static void draw_text_c(const char* s, int x, int y, int w, int h, u32 color, int sc = 1) {
    draw_text(s, x + (w - text_w(s, sc)) / 2, y + (h - 8 * sc) / 2, color, sc);
}
static void draw_int(int v, int x, int y, u32 color, int sc = 1) { char b[16]; itoa_(v, b); draw_text(b, x, y, color, sc); }

// ------------------------------------------------------------
//  Глобальные настройки / прогресс
// ------------------------------------------------------------
static bool use_kb_mouse = true;
static bool use_hayricon = false;
static int  mouse_speed  = 1;          // 1..4
static int  volume       = 50;         // 0..100 (0 = без звука)
static int  coins = 0;
static bool unlocked_teen = false, unlocked_cool = false, has_auto_click = false;
static int  auto_speed_level = 0, click_power_level = 0;

static bool dirty = true;

// ---- Звук (PC speaker) ----
static bool snd_on = false; static u32 snd_end = 0;
static void stop_sound() { outb(0x61, inb(0x61) & 0xFC); snd_on = false; }
static void beep(u32 freq, u32 ms) {
    if (volume == 0 || freq < 20) return;
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6); outb(0x42, (u8)div); outb(0x42, (u8)(div >> 8));
    outb(0x61, inb(0x61) | 3);
    snd_on = true; snd_end = tnow() + ms * tsc16_per_ms;
}

// ---- Сохранение в CMOS ----
static void save_settings() {
    u8 flags = 2;                                  // бит1 — «мышь» (всегда включена)
    if (use_kb_mouse) flags |= 4;
    if (use_hayricon) flags |= 8;
    cmos_write(0x40, flags);
    cmos_write(0x41, (u8)(coins >> 24)); cmos_write(0x42, (u8)(coins >> 16));
    cmos_write(0x43, (u8)(coins >> 8));  cmos_write(0x44, (u8)coins);
    cmos_write(0x45, (unlocked_teen ? 1 : 0) | (unlocked_cool ? 2 : 0) | (has_auto_click ? 4 : 0));
    cmos_write(0x46, (u8)auto_speed_level); cmos_write(0x47, (u8)click_power_level);
    cmos_write(0x48, (u8)volume); cmos_write(0x49, (u8)mouse_speed);
    cmos_write(0x4A, 0xA5);                        // метка «настройки сохранены»
}
static void load_settings() {
    if (cmos_read(0x4A) != 0xA5) return;           // первый запуск — значения по умолчанию
    u8 f = cmos_read(0x40);
    use_kb_mouse = (f & 4) != 0; use_hayricon = (f & 8) != 0;
    coins = (cmos_read(0x41) << 24) | (cmos_read(0x42) << 16) | (cmos_read(0x43) << 8) | cmos_read(0x44);
    u8 g = cmos_read(0x45);
    unlocked_teen = g & 1; unlocked_cool = g & 2; has_auto_click = g & 4;
    auto_speed_level = cmos_read(0x46); click_power_level = cmos_read(0x47);
    volume = cmos_read(0x48); mouse_speed = cmos_read(0x49);
    if (volume > 100) volume = 50;
    if (mouse_speed < 1 || mouse_speed > 4) mouse_speed = 1;
    if (auto_speed_level > 20) auto_speed_level = 20;
    if (click_power_level > 5) click_power_level = 5;
    if (coins < 0) coins = 0;
}

struct IdtPtr { u16 limit; u32 base; } __attribute__((packed));
static void reboot() {
    save_settings();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++);
    outb(0x64, 0xFE);
    static IdtPtr null_idt = {0, 0};                // запасной вариант: triple fault
    asm volatile("lidt %0; int3" :: "m"(null_idt));
    while (1) asm volatile("cli; hlt");
}
static void shutdown() {
    save_settings();
    outw(0x604, 0x2000); outw(0xB004, 0x2000); outw(0x4004, 0x3400);   // QEMU / Bochs / VirtualBox
    while (1) asm volatile("cli; hlt");
}

// ------------------------------------------------------------
//  Часы (RTC)
// ------------------------------------------------------------
static void rtc_time(int& h, int& m) {
    for (int i = 0; i < 100000 && (cmos_read(0x0A) & 0x80); i++);
    u8 hh = cmos_read(4), mm = cmos_read(2), b = cmos_read(0x0B);
    bool pm = (hh & 0x80) != 0; hh &= 0x7F;
    if (!(b & 4)) { hh = (hh & 0x0F) + (hh >> 4) * 10; mm = (mm & 0x0F) + (mm >> 4) * 10; }
    if (!(b & 2)) { if (pm && hh < 12) hh += 12;
    if (!pm && hh == 12) hh = 0; }
    h = hh % 24; m = mm % 60;
}

// ------------------------------------------------------------
//  Мышь PS/2
// ------------------------------------------------------------
namespace mouse {
    int x = 400, y = 300;
    u8  btn = 0; u8 pkt[3]; int idx = 0;
    bool ev_press = false, ev_release = false; int px = 0, py = 0;

    static void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t); }
    static bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t); return t > 0; }
    static void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    static u8   read() { return wait_r() ? inb(0x60) : 0; }
    static void drain() { for (int i = 0; i < 100; i++) { if (!(inb(0x64) & 1)) break; inb(0x60); } }

    void init() {
        x = gfx::W / 2; y = gfx::H / 2;
        drain();
        wait_w(); outb(0x64, 0xA8);                 // включить второй порт (мышь)
        wait_w(); outb(0x64, 0x20);
        u8 status = read();
        status &= ~0x20;                            // тактирование порта мыши
        status &= ~0x02;                            // IRQ12 не нужен — читаем опросом
        wait_w(); outb(0x64, 0x60);
        wait_w(); outb(0x60, status);
        write(0xF6); read();                        // параметры по умолчанию
        write(0xF4); read();                        // включить передачу данных
        drain();
    }
    bool feed(u8 b) {
        if (idx == 0 && !(b & 0x08)) return false;  // потеряна синхронизация пакета
        pkt[idx++] = b;
        if (idx < 3) return false;
        idx = 0;
        if (pkt[0] & 0xC0) return false;            // переполнение — пакет выбрасываем
        int dx = (int)pkt[1] - (((int)pkt[0] << 4) & 0x100);   // знаковые 9 бит
        int dy = (int)pkt[2] - (((int)pkt[0] << 3) & 0x100);
        x += dx * mouse_speed; y -= dy * mouse_speed;
        if (x < 0) x = 0;
        if (x > (int)gfx::W - 1) x = gfx::W - 1;
        if (y < 0) y = 0;
        if (y > (int)gfx::H - 1) y = gfx::H - 1;
        u8 old = btn; btn = pkt[0] & 7;
        if ((btn & 1) && !(old & 1)) { ev_press = true; px = x; py = y; }
        if (!(btn & 1) && (old & 1)) ev_release = true;
        return true;
    }
}

// ------------------------------------------------------------
//  Клавиатура (scancode set 1)
// ------------------------------------------------------------
enum { K_UP = 0x101, K_DOWN, K_LEFT, K_RIGHT, K_DEL, K_F1 = 0x110, K_F2, K_F3 };
static void on_key(int k);

namespace kbd {
    static bool shift = false, e0 = false, caps = false;
    static const char norm[] = "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
    static const char shf[]  = "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

    void feed(u8 b) {
        if (b == 0xE0) { e0 = true; return; }
        bool rel = (b & 0x80) != 0; u8 c = b & 0x7F;
        if (c == 0x2A || c == 0x36) { if (!e0) shift = !rel; e0 = false; return; }
        if (rel) { e0 = false; return; }
        if (e0) {
            e0 = false;
            if (c == 0x48) on_key(K_UP); else if (c == 0x50) on_key(K_DOWN);
            else if (c == 0x4B) on_key(K_LEFT); else if (c == 0x4D) on_key(K_RIGHT);
            else if (c == 0x53) on_key(K_DEL);
            else if (c == 0x1C) on_key('\n');         // Enter на цифровом блоке
            else if (c == 0x35) on_key('/');
            return;
        }
        if (c == 0x4E) { on_key('+'); return; }
        if (c == 0x4A) { on_key('-'); return; }
        { static const char kp[] = "789\0" "456\0" "123" "0."; if (c >= 0x47 && c <= 0x53 && c != 0x4A && c != 0x4E) { char d = kp[c - 0x47]; if (d) { on_key(d); return; } } }
        if (c == 0x3A) { caps = !caps; return; }
        if (c == 0x3B) { on_key(K_F1); return; }
        if (c == 0x3C) { on_key(K_F2); return; }
        if (c == 0x3D) { on_key(K_F3); return; }
        if (c >= 58) return;
        char ch = shift ? shf[c] : norm[c];
        if (caps && ch >= 'a' && ch <= 'z') ch -= 32;
        else if (caps && ch >= 'A' && ch <= 'Z' && shift) ch += 32;
        if (ch) on_key((unsigned char)ch);
    }
}

// ------------------------------------------------------------
//  Палитра и общие помощники интерфейса
// ------------------------------------------------------------
#define SCRW ((int)gfx::W)
#define SCRH ((int)gfx::H)
#define TASKBAR_H 44
#define TITLEH 30

#define C_ACC    0x3B82F6
#define C_ACC_D  0x2563EB
#define C_BG     0xF4F6FA
#define C_PANEL  0xE3E8F2
#define C_TEXT   0x1F2937
#define C_GRAY   0x9CA3AF
#define C_RED    0xEF4444
#define C_GREEN  0x22C55E

struct R { int x, y, w, h; };
static inline bool hit(const R& r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }
static inline bool hov(const R& r) { return hit(r, mouse::x, mouse::y); }

static void draw_button(const R& r, const char* label, u32 col, u32 tcol, bool enabled = true) {
    u32 c = col;
    if (!enabled) { c = 0xD1D5DB; tcol = 0x8B93A1; }
    else if (hov(r)) c = gfx::lerp(col, 0xFFFFFF, 1, 4);
    gfx::fill_round(r.x, r.y, r.w, r.h, 6, c);
    draw_text_c(label, r.x, r.y, r.w, r.h, tcol);
}
static void draw_toggle(int x, int y, bool on) {
    gfx::fill_round(x, y, 52, 26, 13, on ? C_GREEN : 0xB8BFCC);
    gfx::fill_circle(on ? x + 39 : x + 13, y + 13, 10, 0xFFFFFF);
}

// ------------------------------------------------------------
//  Файловая система в памяти
// ------------------------------------------------------------
#define MAXN    96
#define NAMELEN 24
#define DATALEN 400
struct Node { bool used, dir, trashed; int parent; char name[NAMELEN]; int len; char data[DATALEN]; };
static Node fs[MAXN];
static int fs_docs = -1, fs_pics = -1, fs_music = -1;

static int fs_new(int parent, const char* name, bool dir) {
    for (int i = 1; i < MAXN; i++) if (!fs[i].used) {
        fs[i].used = true; fs[i].dir = dir; fs[i].trashed = false; fs[i].parent = parent;
        scopy(fs[i].name, name, NAMELEN); fs[i].len = 0; fs[i].data[0] = 0;
        return i;
    }
    return -1;
}
static bool fs_name_taken(int parent, const char* name, int self) {
    for (int i = 1; i < MAXN; i++)
        if (i != self && fs[i].used && !fs[i].trashed && fs[i].parent == parent && seq(fs[i].name, name)) return true;
    return false;
}
static void fs_unique(int parent, const char* name, int self, char* out) {
    char base[NAMELEN]; scopy(base, name, NAMELEN - 6);
    scopy(out, base, NAMELEN);
    for (int n = 2; n < 100 && fs_name_taken(parent, out, self); n++) {
        int p = sappend(out, 0, base, NAMELEN); p = sappend(out, p, " (", NAMELEN);
        char nb[8]; itoa_(n, nb); p = sappend(out, p, nb, NAMELEN); sappend(out, p, ")", NAMELEN);
    }
}
static void fs_set_text(int idx, const char* t) { int n = slen(t); if (n > DATALEN - 1) n = DATALEN - 1; for (int i = 0; i < n; i++) fs[idx].data[i] = t[i]; fs[idx].data[n] = 0; fs[idx].len = n; }
static bool fs_reachable(int idx) {
    for (int g = 0; g < MAXN; g++) { if (idx == 0) return true; if (idx < 0 || !fs[idx].used || fs[idx].trashed) return false; idx = fs[idx].parent; }
    return false;
}
static void fs_destroy(int idx) {
    fs[idx].used = false;
    for (int j = 1; j < MAXN; j++) if (fs[j].used && fs[j].parent == idx) fs_destroy(j);
}
static int fs_list(int parent, int* out, int max) {
    int n = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 1; i < MAXN && n < max; i++)
            if (fs[i].used && !fs[i].trashed && fs[i].parent == parent && fs[i].dir == (pass == 0)) out[n++] = i;
    return n;
}
static int fs_trash_list(int* out, int max) {
    int n = 0;
    for (int i = 1; i < MAXN && n < max; i++) if (fs[i].used && fs[i].trashed) out[n++] = i;
    return n;
}
static int fs_trash_count() { int t[MAXN]; return fs_trash_list(t, MAXN); }
static void fs_path(int idx, char* out, int max) {
    int st[16], n = 0;
    while (idx > 0 && n < 16) { st[n++] = idx; idx = fs[idx].parent; }
    if (n == 0) { scopy(out, "/", max); return; }
    int p = 0; out[0] = 0;
    while (n) { p = sappend(out, p, "/", max); p = sappend(out, p, fs[st[--n]].name, max); }
}
static void fs_init() {
    memset(fs, 0, sizeof(fs));
    fs[0].used = true; fs[0].dir = true; fs[0].parent = 0; scopy(fs[0].name, "/", NAMELEN);
    fs_docs = fs_new(0, "Documents", true);
    fs_pics = fs_new(0, "Pictures", true);
    fs_music = fs_new(0, "Music", true);
    int f = fs_new(0, "readme.txt", false);
    fs_set_text(f, "Welcome to iMimi OS PC Edition!\n\nOpen files with a double click.\nDelete sends files to Trash.\nType here to edit this file.");
    f = fs_new(fs_docs, "notes.txt", false); fs_set_text(f, "My notes:\n- build MinimalZero\n- add drivers");
    f = fs_new(fs_docs, "todo.txt", false);  fs_set_text(f, "1. Click the Mimi\n2. Buy upgrades\n3. Win");
    f = fs_new(fs_music, "playlist.txt", false); fs_set_text(f, "Track 1\nTrack 2");
    fs_new(fs_pics, "Mimi", true);
}

// ------------------------------------------------------------
//  Окна
// ------------------------------------------------------------
enum { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET, NAPPS };
static const char* app_title[NAPPS] = { "Files", "Trash", "Mimi Clicker", "Calculator", "Settings" };
struct Win { bool open, min; int x, y, w, h; };
static Win wins[NAPPS];
static int zord[NAPPS];
static int drag_win = -1, drag_dx = 0, drag_dy = 0;
static bool start_open = false, power_open = false;

static void init_windows() {
    static const int sz[NAPPS][2] = { {640, 460}, {540, 400}, {580, 440}, {300, 410}, {460, 400} };
    for (int i = 0; i < NAPPS; i++) {
        wins[i].open = false; wins[i].min = false; wins[i].w = sz[i][0]; wins[i].h = sz[i][1];
        if (wins[i].w > SCRW - 40) wins[i].w = SCRW - 40;
        if (wins[i].h > SCRH - TASKBAR_H - 20) wins[i].h = SCRH - TASKBAR_H - 20;
        wins[i].x = (SCRW - wins[i].w) / 2 + (i - 2) * 28 + 40;
        wins[i].y = (SCRH - TASKBAR_H - wins[i].h) / 2 + (i - 2) * 22;
        if (wins[i].x < 0) wins[i].x = 0;
        if (wins[i].y < 0) wins[i].y = 0;
        zord[i] = i;
    }
}
static void front(int a) {
    int p = 0; for (int i = 0; i < NAPPS; i++) if (zord[i] == a) p = i;
    for (int i = p; i < NAPPS - 1; i++) zord[i] = zord[i + 1];
    zord[NAPPS - 1] = a;
}
static int top_win() {
    for (int i = NAPPS - 1; i >= 0; i--) { int a = zord[i]; if (wins[a].open && !wins[a].min) return a; }
    return -1;
}
static int win_at(int x, int y) {
    for (int i = NAPPS - 1; i >= 0; i--) {
        int a = zord[i]; Win& w = wins[a];
        if (w.open && !w.min && x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h) return a;
    }
    return -1;
}

// ------------------------------------------------------------
//  Приложение: Files
// ------------------------------------------------------------
static int  fm_dir = 0, fm_sel = -1, fm_mode = 0, fm_edit = -1, fm_scroll = 0;   // mode: 0 обзор, 1 папка, 2 файл, 3 переименование, 4 редактор
static char fm_buf[NAMELEN]; static int fm_buflen = 0;
static bool blink_state = false;

#define FM_SIDE 130
static R fm_btn(int i, int cx, int cy) {
    static const int ws[5] = { 56, 96, 80, 84, 72 };
    int x = cx + FM_SIDE + 8; for (int k = 0; k < i; k++) x += ws[k] + 6;
    return { x, cy + 6, ws[i], 28 };
}
static R fm_side(int i, int cx, int cy) { return { cx, cy + 10 + i * 34, FM_SIDE, 32 }; }
static int fm_rows(int ch) { int n = (ch - 66 - 26) / 30; return n < 1 ? 1 : n; }
static R fm_row(int i, int cx, int cy, int cw) { return { cx + FM_SIDE, cy + 66 + i * 30, cw - FM_SIDE, 30 }; }
static R fm_prompt_box(int cx, int cy, int cw, int ch) { int dw = 360, dh = 130; return { cx + FM_SIDE + (cw - FM_SIDE - dw) / 2, cy + (ch - dh) / 2, dw, dh }; }
static R fm_prompt_ok(const R& b) { return { b.x + b.w - 16 - 90 - 8 - 90, b.y + b.h - 40, 90, 28 }; }
static R fm_prompt_cancel(const R& b) { return { b.x + b.w - 16 - 90, b.y + b.h - 40, 90, 28 }; }
static R fm_editor_back(int cx, int cy) { return { cx + FM_SIDE + 8, cy + 6, 124, 28 }; }

static void fm_validate() { if (!fs_reachable(fm_dir) || !fs[fm_dir].dir) { fm_dir = 0; fm_sel = -1; } if (fm_sel > 0 && (!fs[fm_sel].used || fs[fm_sel].trashed)) fm_sel = -1; }
static void fm_begin_prompt(int mode) {
    fm_mode = mode; fm_buflen = 0; fm_buf[0] = 0;
    if (mode == 3 && fm_sel > 0) { scopy(fm_buf, fs[fm_sel].name, NAMELEN); fm_buflen = slen(fm_buf); }
}
static void fm_confirm_prompt() {
    if (fm_buflen > 0) {
        char nm[NAMELEN];
        if (fm_mode == 1 || fm_mode == 2) {
            fs_unique(fm_dir, fm_buf, -1, nm);
            int n = fs_new(fm_dir, nm, fm_mode == 1);
            if (n > 0) { fm_sel = n; beep(1100, 40); }
        } else if (fm_mode == 3 && fm_sel > 0) {
            fs_unique(fs[fm_sel].parent, fm_buf, fm_sel, nm);
            scopy(fs[fm_sel].name, nm, NAMELEN); beep(1100, 40);
        }
    }
    fm_mode = 0;
}
static void fm_open_node(int idx) {
    if (fs[idx].dir) { fm_dir = idx; fm_sel = -1; fm_scroll = 0; }
    else { fm_mode = 4; fm_edit = idx; }
}
static void files_key(int k) {
    if (fm_mode >= 1 && fm_mode <= 3) {
        if (k == 27) fm_mode = 0;
        else if (k == '\n') fm_confirm_prompt();
        else if (k == '\b') { if (fm_buflen > 0) fm_buf[--fm_buflen] = 0; }
        else if (k >= 32 && k < 127 && k != '/' && fm_buflen < NAMELEN - 2) { fm_buf[fm_buflen++] = (char)k; fm_buf[fm_buflen] = 0; }
    } else if (fm_mode == 4 && fm_edit > 0) {
        Node& n = fs[fm_edit];
        if (k == 27) fm_mode = 0;
        else if (k == '\b') { if (n.len > 0) n.data[--n.len] = 0; }
        else if ((k == '\n' || (k >= 32 && k < 127)) && n.len < DATALEN - 1) { n.data[n.len++] = (char)(k == '\n' ? '\n' : k); n.data[n.len] = 0; }
    }
}

static void files_draw(int cx, int cy, int cw, int ch) {
    fm_validate();
    gfx::fill_rect(cx, cy, cw, ch, C_BG);
    gfx::fill_rect(cx, cy, FM_SIDE, ch, C_PANEL);
    static const char* side_names[5] = { "Home", "Documents", "Pictures", "Music", "Trash" };
    int side_ids[5] = { 0, fs_docs, fs_pics, fs_music, -2 };
    for (int i = 0; i < 5; i++) {
        R r = fm_side(i, cx, cy);
        bool avail = (i == 0 || i == 4 || (side_ids[i] > 0 && fs_reachable(side_ids[i])));
        bool cur = (i < 4 && avail && fm_dir == side_ids[i] && fm_mode != 4);
        if (cur) gfx::fill_round(r.x + 6, r.y, r.w - 12, r.h, 6, C_ACC);
        else if (avail && hov(r)) gfx::fill_round(r.x + 6, r.y, r.w - 12, r.h, 6, 0xD2DAEA);
        draw_text(side_names[i], r.x + 16, r.y + 12, cur ? 0xFFFFFF : (avail ? C_TEXT : C_GRAY));
    }
    int x0 = cx + FM_SIDE;
    if (fm_mode == 4 && fm_edit > 0 && fs[fm_edit].used) {
        Node& n = fs[fm_edit];
        draw_button(fm_editor_back(cx, cy), "< Save & Back", C_ACC, 0xFFFFFF);
        draw_text_n(n.name, x0 + 146, cy + 16, C_TEXT, 24);
        char cnt[24]; int p = itoa_(n.len, cnt); p = sappend(cnt, p, "/", 24); char t[8]; itoa_(DATALEN - 1, t); sappend(cnt, p, t, 24);
        draw_text(cnt, cx + cw - 8 - text_w(cnt), cy + 16, C_GRAY);
        int ax = x0 + 8, ay = cy + 44, aw = cw - FM_SIDE - 16, ah = ch - 52;
        gfx::fill_rect(ax, ay, aw, ah, 0xFFFFFF); gfx::rect_outline(ax, ay, aw, ah, 0xC9D1E0);
        int cols = (aw - 16) / 8, rows = (ah - 16) / 10; if (cols < 4) cols = 4; if (rows < 1) rows = 1;
        int total = 1, col = 0;
        for (int i = 0; i < n.len; i++) { if (n.data[i] == '\n') { total++; col = 0; } else if (++col >= cols) { total++; col = 0; } }
        int skip = total > rows ? total - rows : 0, row = 0; col = 0;
        for (int i = 0; i <= n.len; i++) {
            if (i == n.len) { if (blink_state && row >= skip) draw_char('_', ax + 8 + col * 8, ay + 8 + (row - skip) * 10, C_ACC_D); break; }
            char c = n.data[i];
            if (c == '\n') { row++; col = 0; continue; }
            if (row >= skip && row - skip < rows) draw_char(c, ax + 8 + col * 8, ay + 8 + (row - skip) * 10, C_TEXT);
            if (++col >= cols) { row++; col = 0; }
        }
        return;
    }
    // панель инструментов
    static const char* bl[5] = { "< Up", "+ Folder", "+ File", "Rename", "Delete" };
    for (int i = 0; i < 5; i++) {
        bool en = (i == 0) ? (fm_dir != 0) : (i < 3 ? true : fm_sel > 0);
        draw_button(fm_btn(i, cx, cy), bl[i], i == 4 ? C_RED : C_ACC, 0xFFFFFF, en);
    }
    char path[96]; fs_path(fm_dir, path, 96);
    gfx::fill_rect(x0, cy + 40, cw - FM_SIDE, 24, 0xFFFFFF);
    draw_text("Path: ", x0 + 10, cy + 48, C_GRAY); draw_text_n(path, x0 + 58, cy + 48, C_TEXT, 50);
    int list[MAXN]; int n = fs_list(fm_dir, list, MAXN);
    int rows = fm_rows(ch);
    if (fm_scroll > n - rows) fm_scroll = n - rows;
    if (fm_scroll < 0) fm_scroll = 0;
    for (int i = 0; i < rows && i + fm_scroll < n; i++) {
        int idx = list[i + fm_scroll]; R r = fm_row(i, cx, cy, cw);
        if (idx == fm_sel) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xCFE0FF);
        else if (hov(r) && fm_mode == 0) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xE8EEFA);
        if (fs[idx].dir) { gfx::fill_round(r.x + 12, r.y + 6, 10, 5, 2, 0xE0A800); gfx::fill_round(r.x + 10, r.y + 9, 24, 16, 3, 0xF5C542); }
        else { gfx::fill_round(r.x + 14, r.y + 5, 17, 21, 2, 0xFFFFFF); gfx::rect_outline(r.x + 14, r.y + 5, 17, 21, 0x9AA3B2); for (int k = 0; k < 3; k++) gfx::fill_rect(r.x + 17, r.y + 10 + k * 5, 11, 1, 0xB8C0CC); }
        draw_text_n(fs[idx].name, r.x + 46, r.y + 11, C_TEXT, 30);
        char info[24]; if (fs[idx].dir) scopy(info, "Folder", 24); else { int p = itoa_(fs[idx].len, info); sappend(info, p, " B", 24); }
        draw_text(info, r.x + r.w - 12 - text_w(info), r.y + 11, C_GRAY);
    }
    if (n == 0) draw_text("This folder is empty", x0 + 20, cy + 90, C_GRAY);
    gfx::fill_rect(x0, cy + ch - 24, cw - FM_SIDE, 24, C_PANEL);
    char st[40]; int p = itoa_(n, st); sappend(st, p, n == 1 ? " item" : " items", 40);
    draw_text(st, x0 + 10, cy + ch - 16, C_TEXT);
    if (fm_mode >= 1 && fm_mode <= 3) {
        gfx::fill_rect_a(cx, cy, cw, ch, 0x000000, 90);
        R b = fm_prompt_box(cx, cy, cw, ch);
        gfx::fill_round(b.x, b.y, b.w, b.h, 10, 0xFFFFFF);
        draw_text(fm_mode == 1 ? "New folder name:" : fm_mode == 2 ? "New file name:" : "Rename to:", b.x + 16, b.y + 20, C_TEXT);
        gfx::fill_rect(b.x + 16, b.y + 42, b.w - 32, 28, 0xF4F6FA); gfx::rect_outline(b.x + 16, b.y + 42, b.w - 32, 28, C_ACC);
        draw_text(fm_buf, b.x + 24, b.y + 52, C_TEXT);
        if (blink_state) draw_char('_', b.x + 24 + fm_buflen * 8, b.y + 52, C_ACC_D);
        draw_button(fm_prompt_ok(b), "OK", C_ACC, 0xFFFFFF);
        draw_button(fm_prompt_cancel(b), "Cancel", 0x6B7280, 0xFFFFFF);
    }
}

static void files_click(int px, int py, int cx, int cy, int cw, int ch) {
    fm_validate();
    if (fm_mode >= 1 && fm_mode <= 3) {
        R b = fm_prompt_box(cx, cy, cw, ch);
        if (hit(fm_prompt_ok(b), px, py)) fm_confirm_prompt();
        else if (hit(fm_prompt_cancel(b), px, py)) fm_mode = 0;
        return;
    }
    if (fm_mode == 4) { if (hit(fm_editor_back(cx, cy), px, py)) { fm_mode = 0; beep(900, 30); } return; }
    int side_ids[5] = { 0, fs_docs, fs_pics, fs_music, -2 };
    for (int i = 0; i < 5; i++) if (hit(fm_side(i, cx, cy), px, py)) {
        if (i == 4) { wins[APP_TRASH].open = true; wins[APP_TRASH].min = false; front(APP_TRASH); }
        else if (i == 0 || (side_ids[i] > 0 && fs_reachable(side_ids[i]))) { fm_dir = side_ids[i]; fm_sel = -1; fm_scroll = 0; }
        return;
    }
    for (int i = 0; i < 5; i++) if (hit(fm_btn(i, cx, cy), px, py)) {
        if (i == 0 && fm_dir != 0) { fm_sel = fm_dir; fm_dir = fs[fm_dir].parent; fm_scroll = 0; }
        else if (i == 1) fm_begin_prompt(1);
        else if (i == 2) fm_begin_prompt(2);
        else if (i == 3 && fm_sel > 0) fm_begin_prompt(3);
        else if (i == 4 && fm_sel > 0) { fs[fm_sel].trashed = true; fm_sel = -1; beep(500, 60); }
        return;
    }
    int list[MAXN]; int n = fs_list(fm_dir, list, MAXN); int rows = fm_rows(ch);
    for (int i = 0; i < rows && i + fm_scroll < n; i++) if (hit(fm_row(i, cx, cy, cw), px, py)) {
        int idx = list[i + fm_scroll]; fm_sel = idx;
        static int last_idx = -1; static u32 last_t = 0;
        if (idx == last_idx && elapsed_ms(last_t) < 450) { last_idx = -1; fm_open_node(idx); beep(1000, 30); }
        else { last_idx = idx; last_t = tnow(); }
        return;
    }
    fm_sel = -1;
}

// ------------------------------------------------------------
//  Приложение: Trash
// ------------------------------------------------------------
static int tr_sel = -1;
static R tr_btn(int i, int cx, int cy) { static const int xs[3] = { 12, 130, 288 }, ws[3] = { 110, 150, 130 }; return { cx + xs[i], cy + 8, ws[i], 30 }; }
static R tr_row(int i, int cx, int cy, int cw) { return { cx, cy + 66 + i * 30, cw, 30 }; }
static void trash_restore(int idx) {
    fs[idx].trashed = false;
    if (!fs_reachable(fs[idx].parent) || !fs[fs[idx].parent].dir) fs[idx].parent = 0;
    char nm[NAMELEN]; fs_unique(fs[idx].parent, fs[idx].name, idx, nm); scopy(fs[idx].name, nm, NAMELEN);
    beep(1300, 40);
}
static void trash_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, C_BG);
    int list[MAXN]; int n = fs_trash_list(list, MAXN);
    if (tr_sel > 0 && (!fs[tr_sel].used || !fs[tr_sel].trashed)) tr_sel = -1;
    gfx::fill_rect(cx, cy, cw, 46, C_PANEL);
    draw_button(tr_btn(0, cx, cy), "Restore", C_GREEN, 0xFFFFFF, tr_sel > 0);
    draw_button(tr_btn(1, cx, cy), "Delete forever", C_RED, 0xFFFFFF, tr_sel > 0);
    draw_button(tr_btn(2, cx, cy), "Empty Trash", 0x6B7280, 0xFFFFFF, n > 0);
    char t[32]; int p = itoa_(n, t); sappend(t, p, n == 1 ? " item in Trash" : " items in Trash", 32);
    draw_text(t, cx + 12, cy + 50, C_GRAY);
    int rows = (ch - 66 - 4) / 30;
    for (int i = 0; i < rows && i < n; i++) {
        int idx = list[i]; R r = tr_row(i, cx, cy, cw);
        if (idx == tr_sel) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xCFE0FF);
        else if (hov(r)) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xE8EEFA);
        int ry = r.y;
        if (fs[idx].dir) { gfx::fill_round(r.x + 12, ry + 6, 10, 5, 2, 0xE0A800); gfx::fill_round(r.x + 10, ry + 9, 24, 16, 3, 0xF5C542); }
        else { gfx::fill_round(r.x + 14, ry + 5, 17, 21, 2, 0xFFFFFF); gfx::rect_outline(r.x + 14, ry + 5, 17, 21, 0x9AA3B2); }
        draw_text_n(fs[idx].name, r.x + 46, ry + 11, C_TEXT, 28);
        draw_text(fs[idx].dir ? "Folder" : "File", r.x + r.w - 70, ry + 11, C_GRAY);
    }
    if (n == 0) draw_text_c("Trash is empty", cx, cy + 60, cw, ch - 60, C_GRAY);
}
static void trash_click(int px, int py, int cx, int cy, int cw, int ch) {
    int list[MAXN]; int n = fs_trash_list(list, MAXN);
    if (hit(tr_btn(0, cx, cy), px, py) && tr_sel > 0) { trash_restore(tr_sel); tr_sel = -1; return; }
    if (hit(tr_btn(1, cx, cy), px, py) && tr_sel > 0) { fs_destroy(tr_sel); tr_sel = -1; beep(400, 80); return; }
    if (hit(tr_btn(2, cx, cy), px, py) && n > 0) { for (int i = 0; i < n; i++) fs_destroy(list[i]); tr_sel = -1; beep(350, 120); return; }
    int rows = (ch - 66 - 4) / 30;
    for (int i = 0; i < rows && i < n; i++) {
        R r = tr_row(i, cx, cy, cw);
        if (hit(r, px, py)) {
            tr_sel = list[i];
            static int last = -1; static u32 lt = 0;
            if (last == tr_sel && elapsed_ms(lt) < 450) { last = -1; trash_restore(tr_sel); tr_sel = -1; }
            else { last = tr_sel; lt = tnow(); }
            return;
        }
    }
    tr_sel = -1;
}

// ------------------------------------------------------------
//  Приложение: Settings
// ------------------------------------------------------------
static R set_minus(int i, int cx, int cy, int cw) { return { cx + cw - 150, cy + 16 + i * 58 + 8, 30, 28 }; }
static R set_plus (int i, int cx, int cy, int cw) { return { cx + cw - 70,  cy + 16 + i * 58 + 8, 30, 28 }; }
static R set_tog  (int i, int cx, int cy, int cw) { return { cx + cw - 80,  cy + 16 + i * 58 + 8, 52, 26 }; }

static void settings_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, C_BG);
    static const char* names[4] = { "Keyboard Mouse", "HayrICons", "Mouse speed", "Volume" };
    static const char* desc[4]  = { "Arrows / WASD move cursor, Space clicks", "Alternative Mimi icon", "Pointer speed (1-4)", "0 = sound off (F2 / F3)" };
    for (int i = 0; i < 4; i++) {
        int y = cy + 16 + i * 58;
        gfx::fill_round(cx + 12, y, cw - 24, 50, 8, 0xFFFFFF);
        draw_text(names[i], cx + 26, y + 11, C_TEXT); draw_text(desc[i], cx + 26, y + 29, C_GRAY);
        if (i < 2) { R t = set_tog(i, cx, cy, cw); draw_toggle(t.x, t.y + 8 - 8, i == 0 ? use_kb_mouse : use_hayricon); }
        else {
            R m = set_minus(i, cx, cy, cw), p = set_plus(i, cx, cy, cw);
            draw_button(m, "-", C_ACC, 0xFFFFFF); draw_button(p, "+", C_ACC, 0xFFFFFF);
            char b[8]; itoa_(i == 2 ? mouse_speed : volume, b);
            draw_text_c(b, m.x + 30, m.y, 40, 28, C_TEXT);
        }
    }
    int y = cy + 16 + 4 * 58 + 8;
    char r[40]; int p = sappend(r, 0, "Display: ", 40); char t[12]; itoa_(SCRW, t); p = sappend(r, p, t, 40); p = sappend(r, p, "x", 40); itoa_(SCRH, t); sappend(r, p, t, 40);
    draw_text(r, cx + 16, y, C_TEXT);
    draw_text("Kernel: MinimalZero (C++, 32-bit)", cx + 16, y + 18, C_GRAY);
    draw_text("Settings are saved automatically.", cx + 16, y + 36, C_GRAY);
}
static void settings_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)ch;
    if (hit(set_tog(0, cx, cy, cw), px, py) || hit({ cx + 12, cy + 16, cw - 24, 50 }, px, py)) { use_kb_mouse = !use_kb_mouse; save_settings(); beep(1000, 30); return; }
    if (hit({ cx + 12, cy + 16 + 58, cw - 24, 50 }, px, py)) { use_hayricon = !use_hayricon; save_settings(); beep(1000, 30); return; }
    if (hit(set_minus(2, cx, cy, cw), px, py)) { if (mouse_speed > 1) mouse_speed--; save_settings(); return; }
    if (hit(set_plus(2, cx, cy, cw), px, py))  { if (mouse_speed < 4) mouse_speed++; save_settings(); return; }
    if (hit(set_minus(3, cx, cy, cw), px, py)) { if (volume >= 10) volume -= 10; save_settings(); beep(800, 30); return; }
    if (hit(set_plus(3, cx, cy, cw), px, py))  { if (volume <= 90) volume += 10; save_settings(); beep(800, 30); return; }
}

// ------------------------------------------------------------
//  Приложение: Calculator
// ------------------------------------------------------------
static int calc_display = 0, calc_stored = 0, calc_op = 0; static bool calc_new = true, calc_err = false;
static void calc_clear() { calc_display = 0; calc_stored = 0; calc_op = 0; calc_new = true; calc_err = false; }
static void calc_eval() {
    if (!calc_op) return;
    int a = calc_stored, b = calc_display, r = b;
    if (calc_op == 1) r = a + b; else if (calc_op == 2) r = a - b; else if (calc_op == 3) r = a * b;
    else if (calc_op == 4) { if (b == 0) { calc_err = true; r = 0; } else r = a / b; }
    calc_display = r; calc_op = 0; calc_new = true;
}
static void calc_digit(int d) {
    if (calc_err) calc_clear();
    if (calc_new) { calc_display = d; calc_new = false; }
    else if (calc_display < 100000000 && calc_display > -100000000) calc_display = calc_display * 10 + (calc_display < 0 ? -d : d);
}
static void calc_opr(int op) {
    if (calc_err) calc_clear();
    if (calc_op && !calc_new) calc_eval();
    calc_stored = calc_display; calc_op = op; calc_new = true;
}
static void calc_button(int bi) {
    static const int digit[16] = { 7, 8, 9, -1, 4, 5, 6, -1, 1, 2, 3, -1, -1, 0, -1, -1 };
    beep(1500, 25);
    if (digit[bi] >= 0) calc_digit(digit[bi]);
    else if (bi == 12) calc_clear();
    else if (bi == 14) { calc_eval(); }
    else calc_opr(bi == 3 ? 4 : bi == 7 ? 3 : bi == 11 ? 2 : 1);
}
static bool calc_key(int k) {
    if (k >= '0' && k <= '9') { calc_digit(k - '0'); return true; }
    if (k == '+') { calc_opr(1); return true; } if (k == '-') { calc_opr(2); return true; }
    if (k == '*') { calc_opr(3); return true; } if (k == '/') { calc_opr(4); return true; }
    if (k == '=' || k == '\n') { calc_eval(); return true; }
    if (k == 'c' || k == 'C') { calc_clear(); return true; }
    if (k == '\b') { if (!calc_new) calc_display /= 10; return true; }
    return false;
}
static R calc_btn(int bi, int cx, int cy, int cw, int ch) {
    int r = bi / 4, c = bi % 4, gap = 8;
    int bw = (cw - 24 - 3 * gap) / 4, bh = (ch - 96 - 12 - 3 * gap) / 4;
    return { cx + 12 + c * (bw + gap), cy + 96 + r * (bh + gap), bw, bh };
}
static void calc_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, 0x1B1F2B);
    gfx::fill_round(cx + 12, cy + 12, cw - 24, 70, 8, 0x0F1218);
    char b[16]; if (calc_err) scopy(b, "Error", 16); else itoa_(calc_display, b);
    draw_text(b, cx + cw - 24 - text_w(b, 3), cy + 38, 0xE5E7EB, 3);
    static const char* L[16] = { "7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+" };
    for (int i = 0; i < 16; i++) {
        R r = calc_btn(i, cx, cy, cw, ch);
        bool op = (i % 4 == 3) || i == 14; bool cl = (i == 12);
        u32 col = cl ? C_RED : op ? 0xF59E0B : 0x3A4154;
        if (calc_op && ((i == 3 && calc_op == 4) || (i == 7 && calc_op == 3) || (i == 11 && calc_op == 2) || (i == 15 && calc_op == 1))) col = 0xFFD166;
        draw_button(r, L[i], col, (calc_op && op && col == 0xFFD166) ? 0x000000 : 0xFFFFFF);
    }
}
static void calc_click(int px, int py, int cx, int cy, int cw, int ch) {
    for (int i = 0; i < 16; i++) if (hit(calc_btn(i, cx, cy, cw, ch), px, py)) { calc_button(i); return; }
}

// ------------------------------------------------------------
//  Приложение: Mimi Clicker
// ------------------------------------------------------------
static int current_mimi = 0; static bool squish_on = false; static u32 squish_t = 0; static u32 auto_t = 0;
struct Img { const u16* rle; u32 size; int w, h; };
static Img skin_img(int i) {
    if (i == 1) return { img_adult_rle, img_adult_size, img_adult_w, img_adult_h };
    if (i == 2) return { img_cool_rle, img_cool_size, img_cool_w, img_cool_h };
    return { img_base_rle, img_base_size, img_base_w, img_base_h };
}
static Img icon_img() {
    if (use_hayricon) return { img_hayrulla_rle, img_hayrulla_size, img_hayrulla_w, img_hayrulla_h };
    return { img_base_rle, img_base_size, img_base_w, img_base_h };
}
static int click_gain() { return 1 + click_power_level; }
static int speed_cost() { return 20 + auto_speed_level * 10; }
static int power_cost() { return 30 * (click_power_level + 1); }
static R clk_btn(int i, int cx, int cy, int cw) { return { cx + 320, cy + 64 + i * 50 + (i >= 3 ? 26 : 0), cw - 336, 40 }; }
static R clk_image(int cx, int cy) {
    Img im = skin_img(current_mimi);
    int sq = 0; if (squish_on) { u32 e = elapsed_ms(squish_t); sq = e < 160 ? 14 - (int)(e * 14 / 160) : 0; }
    int dh = 260 - sq, dw = im.w * dh / im.h;
    if (dw > 290) { dw = 290; dh = im.h * dw / im.w - sq; }
    return { cx + 150 - dw / 2, cy + 360 - dh, dw, dh };
}
static void clicker_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_gradient(cx, cy, cw, ch, 0xFFFFFF, 0xDCE7FB);
    gfx::fill_circle(cx + 26, cy + 26, 10, 0xFFC93C); gfx::fill_circle(cx + 26, cy + 26, 6, 0xFFE08A);
    draw_int(coins, cx + 46, cy + 18, 0xB77900, 2);
    char g[24]; int p = sappend(g, 0, "+", 24); char t[8]; itoa_(click_gain(), t); p = sappend(g, p, t, 24); sappend(g, p, " per click", 24);
    draw_text(g, cx + 16, cy + 46, C_GRAY);
    R ir = clk_image(cx, cy); Img im = skin_img(current_mimi);
    gfx::fill_round(cx + 60, cy + 350, 180, 20, 10, 0x000000, 40);
    gfx::draw_image(ir.x, ir.y, ir.w, ir.h, im.rle, im.size, im.w, im.h);
    gfx::fill_round(cx + 306, cy + 36, 2, ch - 56, 1, 0xB8C4DE);
    draw_text("Skins", cx + 320, cy + 44, C_TEXT);
    draw_text("Upgrades", cx + 320, cy + 64 + 150 + 4, C_TEXT);
    char lb[40];
    for (int i = 0; i < 6; i++) {
        R r = clk_btn(i, cx, cy, cw); bool en = true; u32 col = C_ACC; int p2 = 0;
        if (i < 3) {
            static const char* nm[3] = { "Mini", "Teen", "Cool" };
            bool unl = (i == 0) || (i == 1 ? unlocked_teen : unlocked_cool); int cost = i == 1 ? 10 : 25;
            p2 = sappend(lb, 0, nm[i], 40);
            if (current_mimi == i) { p2 = sappend(lb, p2, " (on)", 40); col = C_GREEN; }
            else if (!unl) { p2 = sappend(lb, p2, " - ", 40); char c[8]; itoa_(cost, c); p2 = sappend(lb, p2, c, 40); en = coins >= cost; }
        } else if (i == 3) {
            p2 = sappend(lb, 0, has_auto_click ? "Auto-click (own)" : "Auto-click - 15", 40); if (has_auto_click) col = C_GREEN; else en = coins >= 15;
        } else if (i == 4) {
            p2 = sappend(lb, 0, "Speed +1 - ", 40); char c[8]; itoa_(speed_cost(), c); p2 = sappend(lb, p2, c, 40); en = has_auto_click && coins >= speed_cost();
        } else {
            if (click_power_level >= 5) { p2 = sappend(lb, 0, "Power MAX", 40); col = C_GREEN; }
            else { p2 = sappend(lb, 0, "Power +1 - ", 40); char c[8]; itoa_(power_cost(), c); p2 = sappend(lb, p2, c, 40); en = coins >= power_cost(); }
        }
        (void)p2; draw_button(r, lb, col, 0xFFFFFF, en || col == C_GREEN);
    }
}
static void clicker_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)ch;
    R ir = clk_image(cx, cy);
    if (hit(ir, px, py)) { coins += click_gain(); squish_on = true; squish_t = tnow(); beep(1200, 40); return; }
    for (int i = 0; i < 6; i++) if (hit(clk_btn(i, cx, cy, cw), px, py)) {
        if (i < 3) {
            bool unl = (i == 0) || (i == 1 ? unlocked_teen : unlocked_cool); int cost = i == 1 ? 10 : 25;
            if (unl) current_mimi = i;
            else if (coins >= cost) { coins -= cost; if (i == 1) unlocked_teen = true; else unlocked_cool = true; current_mimi = i; beep(1600, 80); }
        } else if (i == 3) { if (!has_auto_click && coins >= 15) { coins -= 15; has_auto_click = true; auto_t = tnow(); beep(1600, 80); } }
        else if (i == 4) { if (has_auto_click && coins >= speed_cost() && auto_speed_level < 20) { coins -= speed_cost(); auto_speed_level++; beep(1600, 80); } }
        else { if (click_power_level < 5 && coins >= power_cost()) { coins -= power_cost(); click_power_level++; beep(1600, 80); } }
        save_settings(); return;
    }
}

// ------------------------------------------------------------
//  Рисование окон / рабочего стола
// ------------------------------------------------------------
static void app_draw(int a, int cx, int cy, int cw, int ch) {
    switch (a) {
        case APP_FILES: files_draw(cx, cy, cw, ch); break;
        case APP_TRASH: trash_draw(cx, cy, cw, ch); break;
        case APP_CLICK: clicker_draw(cx, cy, cw, ch); break;
        case APP_CALC:  calc_draw(cx, cy, cw, ch); break;
        case APP_SET:   settings_draw(cx, cy, cw, ch); break;
    }
}
static void app_click(int a, int px, int py, int cx, int cy, int cw, int ch) {
    switch (a) {
        case APP_FILES: files_click(px, py, cx, cy, cw, ch); break;
        case APP_TRASH: trash_click(px, py, cx, cy, cw, ch); break;
        case APP_CLICK: clicker_click(px, py, cx, cy, cw, ch); break;
        case APP_CALC:  calc_click(px, py, cx, cy, cw, ch); break;
        case APP_SET:   settings_click(px, py, cx, cy, cw, ch); break;
    }
}

// иконки приложений (56x56)
static void draw_app_icon(int a, int x, int y) {
    if (a == APP_FILES) {
        gfx::fill_round(x + 4, y + 8, 24, 12, 3, 0xE0A800);
        gfx::fill_round(x + 2, y + 15, 52, 38, 6, 0xF5C542);
        gfx::fill_round(x + 2, y + 24, 52, 29, 6, 0xFFD75E);
    } else if (a == APP_TRASH) {
        bool full = fs_trash_count() > 0;
        if (full) { gfx::fill_rect(x + 14, y + 0, 16, 12, 0xFFFFFF); gfx::fill_rect(x + 28, y + 2, 14, 11, 0xFDE68A); }
        gfx::fill_round(x + 8, y + 10, 40, 7, 2, 0x9AA3B2); gfx::fill_rect(x + 22, y + 6, 12, 5, 0x9AA3B2);
        gfx::fill_round(x + 11, y + 18, 34, 36, 5, 0xB8C0CC);
        for (int k = 0; k < 3; k++) gfx::fill_rect(x + 19 + k * 8, y + 25, 3, 22, 0x7C8696);
    } else if (a == APP_CLICK) {
        Img im = icon_img(); int dh = 52, dw = im.w * dh / im.h; if (dw > 52) { dw = 52; dh = im.h * dw / im.w; }
        gfx::fill_round(x, y, 56, 56, 12, 0xFFFFFF);
        gfx::draw_image(x + 28 - dw / 2, y + 28 - dh / 2, dw, dh, im.rle, im.size, im.w, im.h);
    } else if (a == APP_CALC) {
        gfx::fill_round(x + 6, y + 1, 44, 54, 7, 0x2D3340);
        gfx::fill_round(x + 11, y + 6, 34, 14, 3, 0x9BE7A7);
        for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) gfx::fill_round(x + 11 + c * 9, y + 25 + r * 7, 7, 5, 1, c == 3 ? 0xF59E0B : 0xE5E7EB);
    } else {
        int cx = x + 28, cy = y + 28;
        static const int dx[8] = { 22, 15, 0, -15, -22, -15, 0, 15 }, dy[8] = { 0, 15, 22, 15, 0, -15, -22, -15 };
        for (int k = 0; k < 8; k++) gfx::fill_round(cx + dx[k] - 5, cy + dy[k] - 5, 10, 10, 3, 0xAEB6C4);
        gfx::fill_circle(cx, cy, 18, 0xAEB6C4); gfx::fill_circle(cx, cy, 8, 0x4B5563);
    }
}
static const int desk_apps[5] = { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET };
static int desk_sel = -1;
static R desk_icon(int i) { return { 18, 18 + i * 100, 88, 92 }; }

static void draw_wallpaper() {
    gfx::fill_gradient(0, 0, SCRW, SCRH, 0x0E1A3F, 0x4A2A6B);
    gfx::fill_circle(SCRW * 3 / 4, SCRH / 3, SCRH / 3, 0x5B7CFA, 40);
    gfx::fill_circle(SCRW / 3, SCRH * 3 / 4, SCRH / 4, 0xFF6FB5, 34);
    gfx::fill_circle(SCRW - 120, SCRH - 160, 110, 0x7DE3D0, 30);
    draw_text("iMimi OS", SCRW - 8 * 8 * 2 - 24, SCRH - TASKBAR_H - 40, 0xFFFFFF, 2);
}
static void draw_desktop_icons() {
    for (int i = 0; i < 5; i++) {
        R r = desk_icon(i);
        if (desk_sel == i) gfx::fill_round(r.x, r.y, r.w, r.h, 10, 0xFFFFFF, 70);
        else if (hov(r)) gfx::fill_round(r.x, r.y, r.w, r.h, 10, 0xFFFFFF, 34);
        draw_app_icon(desk_apps[i], r.x + 16, r.y + 6);
        const char* t = app_title[desk_apps[i]];
        int tw = text_w(t); if (tw > r.w) tw = r.w;
        draw_text(t, r.x + (r.w - text_w(t)) / 2 + 1, r.y + 71, 0x000000);
        draw_text(t, r.x + (r.w - text_w(t)) / 2, r.y + 70, 0xFFFFFF);
    }
}
static R win_close_btn(const Win& w) { return { w.x + w.w - 30, w.y + 4, 24, 22 }; }
static R win_min_btn(const Win& w)   { return { w.x + w.w - 58, w.y + 4, 24, 22 }; }
static void draw_window(int a) {
    Win& w = wins[a]; bool active = (top_win() == a);
    gfx::fill_round(w.x + 4, w.y + 6, w.w, w.h, 10, 0x000000, 70);
    gfx::fill_round(w.x, w.y, w.w, TITLEH + 12, 10, active ? 0x2D4A9A : 0x353A50);
    gfx::fill_gradient(w.x + 4, w.y + 2, w.w - 8, TITLEH / 2, active ? 0x3C5DB8 : 0x40465F, active ? 0x2D4A9A : 0x353A50);
    draw_text(app_title[a], w.x + 14, w.y + 11, 0xFFFFFF);
    R m = win_min_btn(w), c = win_close_btn(w);
    gfx::fill_round(m.x, m.y, m.w, m.h, 5, hov(m) ? 0x6B7BB5 : 0x4A5A94); draw_text_c("-", m.x, m.y, m.w, m.h, 0xFFFFFF);
    gfx::fill_round(c.x, c.y, c.w, c.h, 5, hov(c) ? C_RED : 0x4A5A94);    draw_text_c("x", c.x, c.y, c.w, c.h, 0xFFFFFF);
    app_draw(a, w.x, w.y + TITLEH, w.w, w.h - TITLEH);
    gfx::rect_outline(w.x, w.y + TITLEH, w.w, w.h - TITLEH, active ? 0x2D4A9A : 0x353A50);
}

// панель задач и меню «Пуск»
static R start_btn() { return { 6, SCRH - TASKBAR_H + 6, 96, 32 }; }
static R taskbar_btn(int slot) { return { 112 + slot * 148, SCRH - TASKBAR_H + 6, 142, 32 }; }
#define SM_W 230
#define SM_ITEMS 7
static R sm_rect() { int h = 44 + SM_ITEMS * 34 + 10; return { 6, SCRH - TASKBAR_H - 6 - h, SM_W, h }; }
static R sm_item(int i) { R s = sm_rect(); return { s.x + 8, s.y + 40 + i * 34 + (i >= 5 ? 6 : 0), s.w - 16, 32 }; }
static int clock_h = 0, clock_m = 0;

static void draw_taskbar() {
    int ty = SCRH - TASKBAR_H;
    gfx::fill_rect_a(0, ty, SCRW, TASKBAR_H, 0x10142A, 215);
    gfx::fill_rect(0, ty, SCRW, 1, 0x4C5C9A);
    R s = start_btn();
    gfx::fill_round(s.x, s.y, s.w, s.h, 8, start_open ? C_ACC_D : (hov(s) ? 0x5B9BFF : C_ACC));
    draw_text_c("iMimi", s.x, s.y, s.w, s.h, 0xFFFFFF);
    int slot = 0; int tw = top_win();
    for (int a = 0; a < NAPPS; a++) if (wins[a].open) {
        R r = taskbar_btn(slot++);
        bool act = (tw == a);
        gfx::fill_round(r.x, r.y, r.w, r.h, 8, act ? 0x3D5CB8 : (hov(r) ? 0x2B3566 : 0x1F2748));
        if (act) gfx::fill_rect(r.x + 10, r.y + r.h - 3, r.w - 20, 2, 0x8FB4FF);
        draw_text_n(app_title[a], r.x + 10, r.y + 12, wins[a].min ? 0x8892B8 : 0xFFFFFF, 16);
    }
    char c[8]; c[0] = '0' + clock_h / 10; c[1] = '0' + clock_h % 10; c[2] = ':'; c[3] = '0' + clock_m / 10; c[4] = '0' + clock_m % 10; c[5] = 0;
    draw_text(c, SCRW - 16 - text_w(c, 2), ty + 14, 0xFFFFFF, 2);
    char v[16]; int p = sappend(v, 0, "VOL ", 16); char t[8]; itoa_(volume, t); sappend(v, p, t, 16);
    draw_text(v, SCRW - 16 - text_w(c, 2) - 24 - text_w(v), ty + 18, 0xB4BCE0);
}
static void draw_startmenu() {
    R s = sm_rect();
    gfx::fill_round(s.x + 3, s.y + 4, s.w, s.h, 12, 0x000000, 80);
    gfx::fill_round(s.x, s.y, s.w, s.h, 12, 0x181D38);
    gfx::rect_outline(s.x + 1, s.y + 1, s.w - 2, s.h - 2, 0x30396B);
    draw_text("iMimi OS  PC Edition", s.x + 16, s.y + 16, 0xB4BCE0);
    static const char* items[SM_ITEMS] = { "Files", "Trash", "Mimi Clicker", "Calculator", "Settings", "Restart", "Shut down" };
    for (int i = 0; i < SM_ITEMS; i++) {
        R r = sm_item(i);
        if (hov(r)) gfx::fill_round(r.x, r.y, r.w, r.h, 8, i >= 5 ? 0x7A2B34 : 0x2B3566);
        if (i < 5) { static const u32 sw[5] = { 0xF5C542, 0xB8C0CC, 0xFFFFFF, 0x2D3340, 0xAEB6C4 }; gfx::fill_round(r.x + 6, r.y + 6, 20, 20, 5, sw[i]); }
        draw_text(items[i], r.x + 44, r.y + 12, 0xFFFFFF);
    }
}
static R pw_box() { return { (SCRW - 380) / 2, (SCRH - 190) / 2, 380, 190 }; }
static R pw_btn(int i) { R b = pw_box(); if (i == 2) return { b.x + 100, b.y + 130, 180, 36 }; return { b.x + 20 + i * 180, b.y + 70, 160, 44 }; }
static void draw_power() {
    gfx::fill_rect_a(0, 0, SCRW, SCRH, 0x000000, 130);
    R b = pw_box();
    gfx::fill_round(b.x, b.y, b.w, b.h, 12, 0x181D38);
    draw_text_c("Power", b.x, b.y + 14, b.w, 16, 0xFFFFFF, 2);
    draw_button(pw_btn(0), "Restart", 0xF59E0B, 0x000000);
    draw_button(pw_btn(1), "Shut down", C_RED, 0xFFFFFF);
    draw_button(pw_btn(2), "Cancel", 0x4B5563, 0xFFFFFF);
}

static const char* cursor_bmp[19] = {
    "X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
    "X.......X   ", "X........X  ", "X.........X ", "X......XXXXX", "X...X..X    ", "X..X X..X   ", "X.X  X..X   ", "XX    X..X  ",
    "X     X..X  ", "       X..X ", "       XX   " };
static void draw_cursor(int x, int y) {
    for (int j = 0; j < 19; j++) for (int i = 0; i < 12; i++) {
        char c = cursor_bmp[j][i];
        if (c == 'X') gfx::put_pixel(x + i, y + j, 0x000000);
        else if (c == '.') gfx::put_pixel(x + i, y + j, 0xFFFFFF);
    }
}

static void render() {
    draw_wallpaper();
    draw_desktop_icons();
    for (int i = 0; i < NAPPS; i++) { int a = zord[i]; if (wins[a].open && !wins[a].min) draw_window(a); }
    draw_taskbar();
    if (start_open) draw_startmenu();
    if (power_open) draw_power();
    draw_cursor(mouse::x, mouse::y);
}

// ------------------------------------------------------------
//  Обработка ввода
// ------------------------------------------------------------
static void open_app(int a) {
    if (!wins[a].open) { wins[a].open = true;
    if (a == APP_FILES) fm_mode = 0; }
    wins[a].min = false; front(a); beep(1000, 40);
}
static void close_app(int a) {
    wins[a].open = false; wins[a].min = false;
    if (a == APP_FILES) fm_mode = 0;
    if (a == APP_CLICK) save_settings();
    beep(700, 40);
}
static void handle_press(int px, int py) {
    dirty = true;
    if (power_open) {
        if (hit(pw_btn(0), px, py)) reboot();
        else if (hit(pw_btn(1), px, py)) shutdown();
        else power_open = false;
        return;
    }
    if (start_open) {
        if (hit(sm_rect(), px, py)) {
            for (int i = 0; i < SM_ITEMS; i++) if (hit(sm_item(i), px, py)) {
                start_open = false;
                if (i < 5) open_app(i); else if (i == 5) reboot(); else shutdown();
                return;
            }
            return;
        }
        start_open = false;
        if (hit(start_btn(), px, py)) return;      // повторный клик по «Пуск» просто закрывает меню
    }
    if (py >= SCRH - TASKBAR_H) {
        if (hit(start_btn(), px, py)) { start_open = true; return; }
        int slot = 0;
        for (int a = 0; a < NAPPS; a++) if (wins[a].open) {
            if (hit(taskbar_btn(slot++), px, py)) {
                if (wins[a].min) { wins[a].min = false; front(a); }
                else if (top_win() == a) wins[a].min = true;
                else front(a);
                return;
            }
        }
        return;
    }
    int a = win_at(px, py);
    if (a >= 0) {
        front(a); Win& w = wins[a];
        if (py < w.y + TITLEH) {
            if (hit(win_close_btn(w), px, py)) { close_app(a); return; }
            if (hit(win_min_btn(w), px, py)) { w.min = true; return; }
            drag_win = a; drag_dx = px - w.x; drag_dy = py - w.y; return;
        }
        app_click(a, px, py, w.x, w.y + TITLEH, w.w, w.h - TITLEH);
        return;
    }
    for (int i = 0; i < 5; i++) if (hit(desk_icon(i), px, py)) {
        static int last = -1; static u32 lt = 0;
        desk_sel = i;
        if (last == i && elapsed_ms(lt) < 450) { last = -1; open_app(desk_apps[i]); }
        else { last = i; lt = tnow(); }
        return;
    }
    desk_sel = -1;
}
static void handle_release() { drag_win = -1; dirty = true; }

static void on_key(int k) {
    dirty = true;
    if (k == K_F1) { power_open = !power_open; start_open = false; return; }
    if (k == K_F2) { if (volume < 100) volume += 10; save_settings(); beep(900, 30); return; }
    if (k == K_F3) { if (volume > 0) volume -= 10; save_settings(); beep(700, 30); return; }
    if (power_open)  { if (k == 27) power_open = false; return; }
    if (start_open)  { if (k == 27) start_open = false; return; }
    int t = top_win();
    if (t == APP_FILES && fm_mode != 0) { files_key(k); return; }
    if (t == APP_CALC && calc_key(k)) return;
    if (k == 27) { if (t >= 0) close_app(t); return; }
    if (!use_kb_mouse) return;
    int step = 12 * mouse_speed;
    if (k == K_UP    || k == 'w') mouse::y -= step;
    if (k == K_DOWN  || k == 's') mouse::y += step;
    if (k == K_LEFT  || k == 'a') mouse::x -= step;
    if (k == K_RIGHT || k == 'd') mouse::x += step;
    if (mouse::x < 0) mouse::x = 0;
    if (mouse::x > SCRW - 1) mouse::x = SCRW - 1;
    if (mouse::y < 0) mouse::y = 0;
    if (mouse::y > SCRH - 1) mouse::y = SCRH - 1;
    if (k == ' ' || k == '\n') { handle_press(mouse::x, mouse::y); handle_release(); }
}

// ------------------------------------------------------------
//  Загрузочный экран
// ------------------------------------------------------------
static void splash(int stage) {
    u32 bg = use_hayricon ? 0x121212 : 0xFFFFFF, fg = use_hayricon ? 0xFFFFFF : 0x1F2937;
    gfx::fill_rect(0, 0, SCRW, SCRH, bg);
    Img im = icon_img(); int dh = 240, dw = im.w * dh / im.h;
    gfx::draw_image(SCRW / 2 - dw / 2, SCRH / 2 - 190, dw, dh, im.rle, im.size, im.w, im.h);
    draw_text_c("iMimi OS", 0, SCRH / 2 + 70, SCRW, 24, fg, 3);
    draw_text_c("PC Edition", 0, SCRH / 2 + 104, SCRW, 8, 0x888888);
    int bw = 300, bx = SCRW / 2 - bw / 2, by = SCRH / 2 + 140;
    gfx::fill_round(bx, by, bw, 8, 4, use_hayricon ? 0x333333 : 0xE5E7EB);
    gfx::fill_round(bx, by, bw * (stage + 1) / 3, 8, 4, C_ACC);
    gfx::swap();
}

extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic;
    gfx::init(mb);
    if (!gfx::ok) { while (1) asm volatile("cli; hlt"); }
    mouse::init(); load_settings(); fs_init(); init_windows();

    // заставка + калибровка TSC по секундам RTC
    splash(0);
    { u8 last = cmos_read(0); int changes = 0; u32 t0 = 0;
      for (u32 g = 0; g < 30000000u && changes < 2; g++) {
          u8 s = cmos_read(0);
          if (s != last) {
              last = s; changes++;
              if (changes == 1) { t0 = tnow(); splash(1); }
              else { u32 pm = (tnow() - t0) / 1000; if (pm > 1000 && pm < 5000000) tsc16_per_ms = pm; splash(2); }
          }
      } }
    rtc_time(clock_h, clock_m);
    auto_t = tnow();
    mouse::x = SCRW / 2; mouse::y = SCRH / 2;
    u32 clock_t_ = tnow(); u32 blink_prev = 0;
    dirty = true;

    while (1) {
        // --- ввод ---
        for (int n = 0; n < 128; n++) {
            u8 st = inb(0x64);
            if (!(st & 1)) break;
            u8 b = inb(0x60);
            if (st & 0x20) { if (mouse::feed(b)) dirty = true; }
            else kbd::feed(b);
        }
        if (mouse::ev_press)   { mouse::ev_press = false;   handle_press(mouse::px, mouse::py); }
        if (mouse::ev_release) { mouse::ev_release = false; handle_release(); }
        if (drag_win >= 0 && (mouse::btn & 1)) {
            Win& w = wins[drag_win];
            int nx = mouse::x - drag_dx, ny = mouse::y - drag_dy;
            if (nx < -w.w + 100) nx = -w.w + 100;
            if (nx > SCRW - 100) nx = SCRW - 100;
            if (ny < 0) ny = 0;
            if (ny > SCRH - TASKBAR_H - TITLEH) ny = SCRH - TASKBAR_H - TITLEH;
            if (nx != w.x || ny != w.y) { w.x = nx; w.y = ny; dirty = true; }
        }

        // --- таймеры ---
        if (snd_on && (s32)(tnow() - snd_end) >= 0) stop_sound();
        if (has_auto_click) {
            int iv = 1000 - auto_speed_level * 80; if (iv < 150) iv = 150;
            if ((int)elapsed_ms(auto_t) >= iv) {
                auto_t = tnow(); coins += click_gain();
                if (wins[APP_CLICK].open && !wins[APP_CLICK].min) dirty = true;
            }
        }
        if (squish_on) { if (elapsed_ms(squish_t) >= 180) squish_on = false; dirty = true; }
        if (elapsed_ms(clock_t_) >= 1000) {
            clock_t_ = tnow(); int h, m; rtc_time(h, m);
            if (h != clock_h || m != clock_m) { clock_h = h; clock_m = m; dirty = true; }
        }
        u32 bl = (ms_now() / 500) & 1;
        if (bl != blink_prev) { blink_prev = bl; blink_state = bl != 0;
        if (wins[APP_FILES].open && !wins[APP_FILES].min && fm_mode != 0) dirty = true; }

        // --- рисование ---
        if (dirty) { dirty = false; render(); gfx::swap(); }
        else asm volatile("pause");
    }
}
