// ============================================================
//  iMimi OS — PC Edition (MinimalZero kernel) + Text Browser
// ============================================================

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed int          s32;

static inline void outb(u16 p, u8 v)  { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }

extern "C" {
void* memset(void* d, int c, unsigned int n) { u8* p = (u8*)d; while (n--) *p++ = (u8)c; return d; }
void* memcpy(void* d, const void* s, unsigned int n) { u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d; }
void* memmove(void* d, const void* s, unsigned int n) { u8* a = (u8*)d; const u8* b = (const u8*)s; if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; } return d; }
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

struct Img { const u16* rle; u32 size; int w, h; };

// === Время ===
static u32 tsc16_per_ms = 100000;
static inline u32 tnow() { u32 lo, hi; asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); return (hi << 28) | (lo >> 4); }
static inline u32 elapsed_ms(u32 since) { return (tnow() - since) / tsc16_per_ms; }
static inline u32 ms_now() { return tnow() / tsc16_per_ms; }
static u8 cmos_read(u8 reg) { outb(0x70, reg); return inb(0x71); }
static void cmos_write(u8 reg, u8 val) { outb(0x70, (reg | 0x80)); outb(0x71, val); outb(0x70, 0); }

// === Строки ===
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
static int sappend(char* d, int pos, const char* s, int max) { while (*s && pos < max - 1) d[pos++] = *s++; d[pos] = 0; return pos; }

// === GFX ===
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
            if (W > MAXW) W = MAXW; if (H > MAXH) H = MAXH;
            ok = (bpp == 32 || bpp == 24) && W >= 640 && H >= 480;
        }
    }
    static inline void put_pixel(int x, int y, u32 c) { if ((u32)x >= W || (u32)y >= H) return; back[y * W + x] = c; }
    static inline u32 blend(u32 d, u32 s, u32 a) { u32 rb = (((d & 0xFF00FF) * (256 - a) + (s & 0xFF00FF) * a) >> 8) & 0xFF00FF; u32 g = (((d & 0x00FF00) * (256 - a) + (s & 0x00FF00) * a) >> 8) & 0x00FF00; return rb | g; }
    static inline u32 lerp(u32 c1, u32 c2, int t, int tmax) { int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255; int r2 = (c2 >> 16) & 255, g2 = (c2 >> 8) & 255, b2 = c2 & 255; return (((r1 + (r2 - r1) * t / tmax) << 16) | ((g1 + (g2 - g1) * t / tmax) << 8) | (b1 + (b2 - b1) * t / tmax)); }
    static bool clip(int& x, int& y, int& w, int& h) { if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; } if (x + w > (int)W) w = (int)W - x; if (y + h > (int)H) h = (int)H - y; return w > 0 && h > 0; }
    void fill_rect(int x, int y, int w, int h, u32 c) { if (!clip(x, y, w, h)) return; for (int j = 0; j < h; j++) { u32* row = &back[(y + j) * W + x]; for (int i = 0; i < w; i++) row[i] = c; } }
    void fill_rect_a(int x, int y, int w, int h, u32 c, u32 a) { if (!clip(x, y, w, h)) return; for (int j = 0; j < h; j++) { u32* row = &back[(y + j) * W + x]; for (int i = 0; i < w; i++) row[i] = blend(row[i], c, a); } }
    void fill_gradient(int x, int y, int w, int h, u32 c1, u32 c2) { for (int j = 0; j < h; j++) fill_rect(x, y + j, w, 1, lerp(c1, c2, j, h > 1 ? h - 1 : 1)); }
    void rect_outline(int x, int y, int w, int h, u32 c) { fill_rect(x, y, w, 1, c); fill_rect(x, y + h - 1, w, 1, c); fill_rect(x, y, 1, h, c); fill_rect(x + w - 1, y, 1, h, c); }
    static int isqrt_(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
    static int corner_inset(int r, int dy) { int d = 2 * (r - dy) - 1; int v = 4 * r * r - d * d; if (v < 0) v = 0; return r - isqrt_(v) / 2; }
    void fill_round(int x, int y, int w, int h, int r, u32 c, u32 a = 256) {
        if (r * 2 > h) r = h / 2; if (r * 2 > w) r = w / 2;
        for (int j = 0; j < h; j++) { int dy = j < h - 1 - j ? j : h - 1 - j; int ins = dy < r ? corner_inset(r, dy) : 0; if (a >= 256) fill_rect(x + ins, y + j, w - 2 * ins, 1, c); else fill_rect_a(x + ins, y + j, w - 2 * ins, 1, c, a); }
    }
    void fill_circle(int cx, int cy, int r, u32 c, u32 a = 256) {
        for (int j = -r; j <= r; j++) { int half = isqrt_(r * r - j * j); if (a >= 256) fill_rect(cx - half, cy + j, half * 2 + 1, 1, c); else fill_rect_a(cx - half, cy + j, half * 2 + 1, 1, c, a); }
    }
    static inline u32 rgb565_to_888(u16 c) { return (((c >> 11) & 0x1F) * 255 / 31 << 16) | (((c >> 5) & 0x3F) * 255 / 63 << 8) | ((c & 0x1F) * 255 / 31); }
    void draw_image(int ox, int oy, int dw, int dh, const u16* rle, u32 size, int iw, int ih) {
        u32 idx = 0; int x = 0, y = 0;
        while (idx < size && y < ih) {
            u16 hd = rle[idx++]; u32 len = hd & 0x7FFF; bool run = (hd & 0x8000) != 0; u32 col = 0;
            if (run) col = rgb565_to_888(rle[idx++]);
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
        if (bpp == 32) { u32 p32 = pitch / 4; for (u32 y = 0; y < H; y++) { u32* d = (u32*)fb + y * p32; u32* s = &back[y * W]; for (u32 x = 0; x < W; x++) d[x] = s[x]; } }
        else { for (u32 y = 0; y < H; y++) { u8* d = fb + y * pitch; u32* s = &back[y * W]; for (u32 x = 0; x < W; x++) { u32 c = s[x]; d[x*3] = c & 255; d[x*3+1] = (c >> 8) & 255; d[x*3+2] = (c >> 16) & 255; } } }
    }
}

// === ШРИФТ ===
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
static void draw_char(char c, int x, int y, u32 color, int sc = 1) {
    if (c < 32 || c > 126) c = '?'; const u8* g = font8x8[c - 32];
    for (int r = 0; r < 8; r++) for (int cl = 0; cl < 8; cl++)
        if (g[r] & (1 << (7 - cl))) { if (sc == 1) gfx::put_pixel(x + cl, y + r, color); else gfx::fill_rect(x + cl * sc, y + r * sc, sc, sc, color); }
}
static void draw_text(const char* s, int x, int y, u32 color, int sc = 1) { while (*s) { draw_char(*s++, x, y, color, sc); x += 8 * sc; } }
static void draw_text_n(const char* s, int x, int y, u32 color, int maxc) { int n = 0; while (*s && n < maxc) { draw_char(*s++, x, y, color); x += 8; n++; } }
static int text_w(const char* s, int sc = 1) { return slen(s) * 8 * sc; }
static void draw_text_c(const char* s, int x, int y, int w, int h, u32 color, int sc = 1) { draw_text(s, x + (w - text_w(s, sc)) / 2, y + (h - 8 * sc) / 2, color, sc); }
static void draw_int(int v, int x, int y, u32 color, int sc = 1) { char b[16]; itoa_(v, b); draw_text(b, x, y, color, sc); }

// === НАСТРОЙКИ ===
static bool use_kb_mouse = true, use_hayricon = false;
static int mouse_speed = 1, volume = 50;
static int coins = 0;
static bool unlocked_teen = false, unlocked_cool = false, has_auto_click = false;
static int auto_speed_level = 0, click_power_level = 0;
static bool dirty = true;
static int clock_h = 12, clock_m = 0;
static int current_mimi = 0;
static int clicker_squish = 0;
static u32 auto_t = 0;
static bool blink_state = false;

// === ЗВУК ===
static bool snd_on = false; static u32 snd_end = 0;
static void stop_sound() { outb(0x61, inb(0x61) & 0xFC); snd_on = false; }
static void beep(u32 freq, u32 ms) {
    if (volume == 0 || freq < 20) return;
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6); outb(0x42, (u8)div); outb(0x42, (u8)(div >> 8));
    outb(0x61, inb(0x61) | 3);
    snd_on = true; snd_end = tnow() + ms * tsc16_per_ms;
}

// === СОХРАНЕНИЕ ===
static void save_settings() {
    u8 flags = 2; if (use_kb_mouse) flags |= 4; if (use_hayricon) flags |= 8;
    cmos_write(0x40, flags);
    cmos_write(0x41, (u8)(coins >> 24)); cmos_write(0x42, (u8)(coins >> 16)); cmos_write(0x43, (u8)(coins >> 8)); cmos_write(0x44, (u8)coins);
    cmos_write(0x45, (unlocked_teen ? 1 : 0) | (unlocked_cool ? 2 : 0) | (has_auto_click ? 4 : 0));
    cmos_write(0x46, (u8)auto_speed_level); cmos_write(0x47, (u8)click_power_level);
    cmos_write(0x48, (u8)volume); cmos_write(0x49, (u8)mouse_speed); cmos_write(0x4A, 0xA5);
}
static void load_settings() {
    if (cmos_read(0x4A) != 0xA5) return;
    u8 f = cmos_read(0x40);
    use_kb_mouse = (f & 4) != 0; use_hayricon = (f & 8) != 0;
    coins = (cmos_read(0x41) << 24) | (cmos_read(0x42) << 16) | (cmos_read(0x43) << 8) | cmos_read(0x44);
    u8 g = cmos_read(0x45); unlocked_teen = g & 1; unlocked_cool = g & 2; has_auto_click = g & 4;
    auto_speed_level = cmos_read(0x46); click_power_level = cmos_read(0x47);
    volume = cmos_read(0x48); mouse_speed = cmos_read(0x49);
    if (volume > 100) volume = 50; if (mouse_speed < 1 || mouse_speed > 4) mouse_speed = 1;
    if (auto_speed_level > 20) auto_speed_level = 20; if (click_power_level > 5) click_power_level = 5; if (coins < 0) coins = 0;
}
struct IdtPtr { u16 limit; u32 base; } __attribute__((packed));
static void reboot() { save_settings(); for (int i = 0; i < 100000 && (inb(0x64) & 2); i++); outb(0x64, 0xFE); static IdtPtr null_idt = {0, 0}; asm volatile("lidt %0; int3" :: "m"(null_idt)); while (1) asm volatile("cli; hlt"); }
static void shutdown() { save_settings(); outw(0x604, 0x2000); outw(0xB004, 0x2000); outw(0x4004, 0x3400); while (1) asm volatile("cli; hlt"); }

static void rtc_time(int& h, int& m) {
    for (int i = 0; i < 100000 && (cmos_read(0x0A) & 0x80); i++);
    u8 hh = cmos_read(4), mm = cmos_read(2), b = cmos_read(0x0B);
    bool pm = (hh & 0x80) != 0; hh &= 0x7F;
    if (!(b & 4)) { hh = (hh & 0x0F) + (hh >> 4) * 10; mm = (mm & 0x0F) + (mm >> 4) * 10; }
    if (!(b & 2)) { if (pm && hh < 12) hh += 12; if (!pm && hh == 12) hh = 0; }
    h = hh % 24; m = mm % 60;
}

// === МЫШЬ ===
namespace mouse {
    int x = 400, y = 300; u8 btn = 0; u8 pkt[3]; int idx = 0;
    bool ev_press = false, ev_release = false; int px = 0, py = 0;
    static void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t); }
    static bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t); return t > 0; }
    static void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    static u8 read() { return wait_r() ? inb(0x60) : 0; }
    void init() {
        x = gfx::W / 2; y = gfx::H / 2;
        for (int i = 0; i < 100; i++) { if (!(inb(0x64) & 1)) break; inb(0x60); }
        wait_w(); outb(0x64, 0xA8); wait_w(); outb(0x64, 0x20); u8 status = read(); status &= ~0x20; status &= ~0x02;
        wait_w(); outb(0x64, 0x60); wait_w(); outb(0x60, status); write(0xF6); read(); write(0xF4); read();
    }
    bool feed(u8 b) {
        if (idx == 0 && !(b & 0x08)) return false; pkt[idx++] = b; if (idx < 3) return false; idx = 0;
        if (pkt[0] & 0xC0) return false;
        int dx = (int)pkt[1] - (((int)pkt[0] << 4) & 0x100); int dy = (int)pkt[2] - (((int)pkt[0] << 3) & 0x100);
        x += dx * mouse_speed; y -= dy * mouse_speed;
        if (x < 0) x = 0; if (x > (int)gfx::W - 1) x = gfx::W - 1; if (y < 0) y = 0; if (y > (int)gfx::H - 1) y = gfx::H - 1;
        u8 old = btn; btn = pkt[0] & 7;
        if ((btn & 1) && !(old & 1)) { ev_press = true; px = x; py = y; }
        if (!(btn & 1) && (old & 1)) ev_release = true; return true;
    }
}

// === КЛАВИАТУРА ===
enum { K_UP = 0x101, K_DOWN, K_LEFT, K_RIGHT, K_DEL, K_F1 = 0x110, K_F2, K_F3 };
static void on_key(int k);
namespace kbd {
    static bool shift = false, e0 = false, caps = false;
    static const char norm[] = "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
    static const char shf[]  = "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";
    void feed(u8 b) {
        if (b == 0xE0) { e0 = true; return; } bool rel = (b & 0x80) != 0; u8 c = b & 0x7F;
        if (c == 0x2A || c == 0x36) { if (!e0) shift = !rel; e0 = false; return; } if (rel) { e0 = false; return; }
        if (e0) { e0 = false; if (c == 0x48) on_key(K_UP); else if (c == 0x50) on_key(K_DOWN); else if (c == 0x4B) on_key(K_LEFT); else if (c == 0x4D) on_key(K_RIGHT); else if (c == 0x53) on_key(K_DEL); else if (c == 0x1C) on_key('\n'); else if (c == 0x35) on_key('/'); return; }
        if (c == 0x4E) { on_key('+'); return; } if (c == 0x4A) { on_key('-'); return; }
        { static const char kp[] = "789\0" "456\0" "123" "0."; if (c >= 0x47 && c <= 0x53 && c != 0x4A && c != 0x4E) { char d = kp[c - 0x47]; if (d) { on_key(d); return; } } }
        if (c == 0x3A) { caps = !caps; return; } if (c == 0x3B) { on_key(K_F1); return; } if (c == 0x3C) { on_key(K_F2); return; } if (c == 0x3D) { on_key(K_F3); return; }
        if (c >= 58) return; char ch = shift ? shf[c] : norm[c];
        if (caps && ch >= 'a' && ch <= 'z') ch -= 32; else if (caps && ch >= 'A' && ch <= 'Z' && shift) ch += 32;
        if (ch) on_key((unsigned char)ch);
    }
}

// === ПАЛИТРА ===
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
    u32 c = col; if (!enabled) { c = 0xD1D5DB; tcol = 0x8B93A1; } else if (hov(r)) c = gfx::lerp(col, 0xFFFFFF, 1, 4);
    gfx::fill_round(r.x, r.y, r.w, r.h, 6, c); draw_text_c(label, r.x, r.y, r.w, r.h, tcol);
}
static void draw_toggle(int x, int y, bool on) { gfx::fill_round(x, y, 52, 26, 13, on ? C_GREEN : 0xB8BFCC); gfx::fill_circle(on ? x + 39 : x + 13, y + 13, 10, 0xFFFFFF); }

// === ФАЙЛОВАЯ СИСТЕМА ===
#define MAXN 96
#define NAMELEN 24
#define DATALEN 400
struct Node { bool used, dir, trashed; int parent; char name[NAMELEN]; int len; char data[DATALEN]; };
static Node fs[MAXN];
static int fs_docs = -1, fs_pics = -1, fs_music = -1;
static int fm_dir = 0, fm_sel = -1, fm_scroll = 0;
static int tr_sel = -1;
static int fs_new(int parent, const char* name, bool dir) { for (int i = 1; i < MAXN; i++) if (!fs[i].used) { fs[i].used = true; fs[i].dir = dir; fs[i].trashed = false; fs[i].parent = parent; scopy(fs[i].name, name, NAMELEN); fs[i].len = 0; fs[i].data[0] = 0; return i; } return -1; }
static void fs_set_text(int idx, const char* t) { int n = slen(t); if (n > DATALEN - 1) n = DATALEN - 1; for (int i = 0; i < n; i++) fs[idx].data[i] = t[i]; fs[idx].data[n] = 0; fs[idx].len = n; }
static bool fs_reachable(int idx) { for (int g = 0; g < MAXN; g++) { if (idx == 0) return true; if (idx < 0 || !fs[idx].used || fs[idx].trashed) return false; idx = fs[idx].parent; } return false; }
static void fs_destroy(int idx) { fs[idx].used = false; for (int j = 1; j < MAXN; j++) if (fs[j].used && fs[j].parent == idx) fs_destroy(j); }
static int fs_list(int parent, int* out, int max) { int n = 0; for (int i = 1; i < MAXN && n < max; i++) if (fs[i].used && !fs[i].trashed && fs[i].parent == parent) out[n++] = i; return n; }
static int fs_trash_list(int* out, int max) { int n = 0; for (int i = 1; i < MAXN && n < max; i++) if (fs[i].used && fs[i].trashed) out[n++] = i; return n; }
static int fs_trash_count() { int t[MAXN]; return fs_trash_list(t, MAXN); }
static void fs_path(int idx, char* out, int max) { int st[16], n = 0; while (idx > 0 && n < 16) { st[n++] = idx; idx = fs[idx].parent; } if (n == 0) { scopy(out, "/", max); return; } int p = 0; out[0] = 0; while (n) { p = sappend(out, p, "/", max); p = sappend(out, p, fs[st[--n]].name, max); } }
static void fs_init() {
    memset(fs, 0, sizeof(fs)); fs[0].used = true; fs[0].dir = true; fs[0].parent = 0; scopy(fs[0].name, "/", NAMELEN);
    fs_docs = fs_new(0, "Documents", true); fs_pics = fs_new(0, "Pictures", true); fs_music = fs_new(0, "Music", true);
    int f = fs_new(0, "readme.txt", false); fs_set_text(f, "Welcome to iMimi OS!");
    f = fs_new(fs_docs, "notes.txt", false); fs_set_text(f, "My notes");
    f = fs_new(fs_music, "playlist.txt", false); fs_set_text(f, "Track 1");
    fs_new(fs_pics, "Mimi", true);
}

// === ОКНА ===
enum { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET, APP_BROWSER, APP_GALLERY, NAPPS };
static const char* app_title[NAPPS] = { "Files", "Trash", "Mimi Clicker", "Calculator", "Settings", "Browser", "Gallery" };
struct Win { bool open, min; int x, y, w, h; };
static Win wins[NAPPS];
static int zord[NAPPS];
static int drag_win = -1, drag_dx = 0, drag_dy = 0;
static bool start_open = false, power_open = false;
static void init_windows() {
    static const int sz[NAPPS][2] = { {640,460},{540,400},{580,440},{300,410},{460,400},{640,470},{680,520} };
    for (int i = 0; i < NAPPS; i++) {
        wins[i].open = false; wins[i].min = false; wins[i].w = sz[i][0]; wins[i].h = sz[i][1];
        if (wins[i].w > SCRW - 40) wins[i].w = SCRW - 40; if (wins[i].h > SCRH - TASKBAR_H - 20) wins[i].h = SCRH - TASKBAR_H - 20;
        wins[i].x = (SCRW - wins[i].w) / 2 + (i - 2) * 28 + 40; wins[i].y = (SCRH - TASKBAR_H - wins[i].h) / 2 + (i - 2) * 22;
        if (wins[i].x < 0) wins[i].x = 0; if (wins[i].y < 0) wins[i].y = 0; zord[i] = i;
    }
}
static void front(int a) { int p = 0; for (int i = 0; i < NAPPS; i++) if (zord[i] == a) p = i; for (int i = p; i < NAPPS - 1; i++) zord[i] = zord[i + 1]; zord[NAPPS - 1] = a; }
static int top_win() { for (int i = NAPPS - 1; i >= 0; i--) { int a = zord[i]; if (wins[a].open && !wins[a].min) return a; } return -1; }
static int win_at(int x, int y) { for (int i = NAPPS - 1; i >= 0; i--) { int a = zord[i]; Win& w = wins[a]; if (w.open && !w.min && x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h) return a; } return -1; }

// === БРАУЗЕР ===
// The emulator host bridge fetches pages through COM1; this is a text browser.
static char web_url[128] = "info.cern.ch";
static char web_previous[128] = "";
static char web_loaded_url[128] = "";
static char web_raw[8192];
static char web_text[8192];
static char web_status[96] = "Ready - click address bar to edit";
static bool web_loading = false, web_editing = false, web_has_previous = false;
static int web_scroll = 0;

static void strip_html(const char* html, char* out, int max) {
    int in_tag = 0, o = 0;
    for (int i = 0; html[i] && o < max - 1; i++) {
        char c = html[i];
        if (c == '<') { in_tag = 1; continue; }
        if (c == '>') { in_tag = 0; continue; }
        if (!in_tag) {
            if (c == '\\n' || c == '\\r') {
                if (o > 0 && out[o-1] != '\\n') out[o++] = '\\n';
            } else if (c == '&') {
                if (html[i+1]=='n'&&html[i+2]=='b'&&html[i+3]=='s'&&html[i+4]==';') { out[o++]=' '; i+=4; }
                else if (html[i+1]=='a'&&html[i+2]=='m'&&html[i+3]=='p'&&html[i+4]==';') { out[o++]='&'; i+=4; }
                else if (html[i+1]=='l'&&html[i+2]=='t'&&html[i+3]==';') { out[o++]='<'; i+=3; }
                else if (html[i+1]=='g'&&html[i+2]=='t'&&html[i+3]==';') { out[o++]='>'; i+=3; }
                else if (html[i+1]=='q'&&html[i+2]=='u'&&html[i+3]=='o'&&html[i+4]=='t'&&html[i+5]==';') { out[o++]='"'; i+=5; }
                else if (html[i+1]=='#'&&html[i+2]=='3'&&html[i+3]=='9'&&html[i+4]==';') { out[o++]=39; i+=4; }
                else out[o++] = '&';
            } else if ((unsigned char)c >= 32 || c == '\\n') out[o++] = c;
        }
    }
    out[o] = 0;
}

static void browser_load() {
    if (!web_url[0]) { scopy(web_status, "Enter an address first", sizeof(web_status)); return; }
    web_loading = true;
    web_editing = false;
    web_text[0] = 0;
    web_raw[0] = 0;
    scopy(web_status, "Waiting for emulator host bridge...", sizeof(web_status));
    outb(0xE9, 0x01);
    for (int n = 0; web_url[n]; n++) outb(0xE9, (u8)web_url[n]);
    outb(0xE9, 0);
    int i = 0, timeout = 0;
    while (i < (int)sizeof(web_raw) - 1) {
        if ((inb(0x3F8 + 5) & 1) == 0) {
            if (++timeout > 30000000) break;
            continue;
        }
        int value = inb(0x3F8);
        if (value == 0) break;
        web_raw[i++] = (char)value;
        timeout = 0;
    }
    web_raw[i] = 0;
    if (i == 0) scopy(web_status, "No response from host bridge (COM1)", sizeof(web_status));
    else {
        strip_html(web_raw, web_text, sizeof(web_text));
        scopy(web_status, "Loaded text response", sizeof(web_status));
        if (web_loaded_url[0] && !seq(web_loaded_url, web_url)) {
            scopy(web_previous, web_loaded_url, sizeof(web_previous));
            web_has_previous = true;
        }
        scopy(web_loaded_url, web_url, sizeof(web_loaded_url));
    }
    web_loading = false;
    web_scroll = 0;
}

static void browser_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, 0xFFFFFF);
    gfx::fill_rect(cx, cy, cw, 42, 0xE8ECF3);
    R back = { cx + 7, cy + 7, 38, 28 };
    R reload = { cx + 49, cy + 7, 54, 28 };
    R address = { cx + 109, cy + 7, cw - 184, 28 };
    R go = { cx + cw - 67, cy + 7, 60, 28 };
    draw_button(back, "<", 0xD6DEEA, C_TEXT, web_has_previous);
    draw_button(reload, "Reload", 0xD6DEEA, C_TEXT);
    gfx::fill_round(address.x, address.y, address.w, address.h, 5, 0xFFFFFF);
    gfx::rect_outline(address.x, address.y, address.w, address.h, web_editing ? C_ACC : 0xB8C2D2);
    draw_text_n(web_url, address.x + 7, address.y + 10, C_TEXT, (address.w - 16) / 8);
    if (web_editing && blink_state) {
        int cursor_x = address.x + 7 + text_w(web_url);
        if (cursor_x < address.x + address.w - 5) gfx::fill_rect(cursor_x, address.y + 7, 1, 13, C_ACC);
    }
    draw_button(go, "Go", C_ACC, 0xFFFFFF);
    gfx::fill_rect(cx, cy + 42, cw, 22, 0xF4F6FA);
    draw_text_n(web_status, cx + 9, cy + 49, 0x64748B, (cw - 18) / 8);
    const int content_y = cy + 70, content_h = ch - 74;
    gfx::fill_rect(cx, content_y, cw, content_h, 0xFFFFFF);
    if (web_loading) { draw_text_c("Loading...", cx, content_y + 30, cw, 32, C_ACC, 2); return; }
    if (!web_text[0]) {
        draw_text_c("MinimalZero Text Browser", cx + 8, content_y + 35, cw - 16, 28, C_TEXT, 2);
        draw_text_c("Click address, type a URL, then press Enter or Go.", cx + 8, content_y + 78, cw - 16, 18, C_GRAY);
        draw_text_c("Pages are fetched by the emulator host bridge.", cx + 8, content_y + 104, cw - 16, 18, C_GRAY);
        return;
    }
    int tx = cx + 12, ty = content_y + 10, line = 0;
    const int max_lines = content_h > 20 ? (content_h - 16) / 12 : 1;
    for (int i = 0; web_text[i]; i++) {
        if (line < web_scroll) { if (web_text[i] == '\\n') line++; continue; }
        if (line - web_scroll >= max_lines) break;
        if (web_text[i] == '\\n') { tx = cx + 12; ty += 12; line++; }
        else {
            if (tx > cx + cw - 20) { tx = cx + 12; ty += 12; line++; }
            if (line - web_scroll >= max_lines) break;
            draw_char(web_text[i], tx, ty, C_TEXT);
            tx += 8;
        }
    }
}

static void browser_click(int px, int py, int cx, int cy, int cw, int ch) {
    R back = { cx + 7, cy + 7, 38, 28 };
    R reload = { cx + 49, cy + 7, 54, 28 };
    R address = { cx + 109, cy + 7, cw - 184, 28 };
    R go = { cx + cw - 67, cy + 7, 60, 28 };
    if (hit(back, px, py) && web_has_previous) {
        char current[128]; scopy(current, web_url, sizeof(current));
        scopy(web_url, web_previous, sizeof(web_url));
        scopy(web_previous, current, sizeof(web_previous));
        browser_load(); return;
    }
    if (hit(reload, px, py)) { browser_load(); return; }
    if (hit(address, px, py)) { web_url[0] = 0; web_editing = true; return; }
    if (hit(go, px, py)) { browser_load(); return; }
    if (py >= cy + 70) {
        int lines = 0;
        for (int i = 0; web_text[i]; i++) if (web_text[i] == '\\n') lines++;
        int visible = (ch - 90) / 12;
        if (py < cy + ch / 2 && web_scroll > 0) web_scroll--;
        else if (web_scroll + visible < lines + 1) web_scroll++;
    }
}

// === APP GALLERY ===
static int gallery_selected = -1;
static const Img gallery_images[] = {
    { gallery_0_rle, gallery_0_size, gallery_0_w, gallery_0_h },
    { gallery_1_rle, gallery_1_size, gallery_1_w, gallery_1_h },
    { gallery_2_rle, gallery_2_size, gallery_2_w, gallery_2_h },
    { gallery_3_rle, gallery_3_size, gallery_3_w, gallery_3_h },
    { gallery_4_rle, gallery_4_size, gallery_4_w, gallery_4_h },
    { gallery_5_rle, gallery_5_size, gallery_5_w, gallery_5_h },
    { gallery_6_rle, gallery_6_size, gallery_6_w, gallery_6_h },
    { gallery_7_rle, gallery_7_size, gallery_7_w, gallery_7_h }
};
static void gallery_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, 0xF4F6FA);
    if (gallery_selected >= 0 && gallery_selected < gallery_image_count) {
        R back = { cx + 10, cy + 8, 64, 28 }, prev = { cx + cw - 142, cy + 8, 58, 28 }, next = { cx + cw - 76, cy + 8, 66, 28 };
        draw_button(back, "Back", 0xDCE4F0, C_TEXT);
        draw_button(prev, "Prev", 0xDCE4F0, C_TEXT);
        draw_button(next, "Next", C_ACC, 0xFFFFFF);
        draw_text_n(gallery_image_names[gallery_selected], cx + 88, cy + 18, C_TEXT, 24);
        Img im = gallery_images[gallery_selected];
        int maxw = cw - 36, maxh = ch - 62, dw = maxw, dh = im.h * dw / im.w;
        if (dh > maxh) { dh = maxh; dw = im.w * dh / im.h; }
        if (dw < 1) dw = 1; if (dh < 1) dh = 1;
        int ix = cx + (cw - dw) / 2, iy = cy + 48 + (maxh - dh) / 2;
        gfx::fill_round(ix - 4, iy - 4, dw + 8, dh + 8, 6, 0xFFFFFF);
        gfx::rect_outline(ix - 4, iy - 4, dw + 8, dh + 8, 0xD1D9E6);
        gfx::draw_image(ix, iy, dw, dh, im.rle, im.size, im.w, im.h);
        return;
    }
    draw_text("Pictures from Assetst/", cx + 14, cy + 12, C_TEXT, 2);
    draw_text("Select a photo to open it", cx + 16, cy + 34, 0x64748B);
    const int cols = 3, gap = 10, cellw = (cw - 48) / 3, top = cy + 58, cellh = (ch - 68) / 3;
    for (int i = 0; i < gallery_image_count && i < 9; i++) {
        int x = cx + 14 + (i % cols) * (cellw + gap), y = top + (i / cols) * cellh;
        R tile = { x, y, cellw, cellh - 6 };
        gfx::fill_round(x, y, cellw, cellh - 6, 7, 0xFFFFFF);
        gfx::rect_outline(x, y, cellw, cellh - 6, hov(tile) ? C_ACC : 0xD8DEE9);
        Img im = gallery_images[i];
        int maxw = cellw - 16, maxh = cellh - 34, dw = maxw, dh = im.h * dw / im.w;
        if (dh > maxh) { dh = maxh; dw = im.w * dh / im.h; }
        if (dw < 1) dw = 1; if (dh < 1) dh = 1;
        gfx::draw_image(x + (cellw - dw) / 2, y + 5 + (maxh - dh) / 2, dw, dh, im.rle, im.size, im.w, im.h);
        draw_text_n(gallery_image_names[i], x + 7, y + cellh - 20, C_TEXT, (cellw - 14) / 8);
    }
}
static void gallery_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)ch;
    if (gallery_selected >= 0) {
        R back = { cx + 10, cy + 8, 64, 28 }, prev = { cx + cw - 142, cy + 8, 58, 28 }, next = { cx + cw - 76, cy + 8, 66, 28 };
        if (hit(back, px, py)) { gallery_selected = -1; return; }
        if (hit(prev, px, py)) { gallery_selected = (gallery_selected + gallery_image_count - 1) % gallery_image_count; return; }
        if (hit(next, px, py)) { gallery_selected = (gallery_selected + 1) % gallery_image_count; return; }
        return;
    }
    const int cols = 3, gap = 10, cellw = (cw - 48) / 3, top = cy + 58, cellh = (ch - 68) / 3;
    for (int i = 0; i < gallery_image_count && i < 9; i++) {
        int x = cx + 14 + (i % cols) * (cellw + gap), y = top + (i / cols) * cellh;
        if (hit({x, y, cellw, cellh - 6}, px, py)) { gallery_selected = i; return; }
    }
}

// === APP FILES ===
static void files_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, C_BG); gfx::fill_rect(cx, cy, 130, ch, C_PANEL);
    static const char* side_names[5] = { "Home", "Docs", "Pics", "Music", "Trash" };
    int side_ids[5] = { 0, fs_docs, fs_pics, fs_music, -2 };
    for (int i = 0; i < 5; i++) { R r = { cx, cy + 10 + i * 34, 130, 32 }; bool cur = (i < 4 && fm_dir == side_ids[i]); if (cur) gfx::fill_round(r.x + 6, r.y, r.w - 12, r.h, 6, C_ACC); else if (hov(r)) gfx::fill_round(r.x + 6, r.y, r.w - 12, r.h, 6, 0xD2DAEA); draw_text(side_names[i], r.x + 16, r.y + 12, cur ? 0xFFFFFF : C_TEXT); }
    R b0 = { cx + 138, cy + 6, 80, 28 }; R b1 = { cx + 224, cy + 6, 100, 28 }; R b2 = { cx + 330, cy + 6, 80, 28 };
    draw_button(b0, "< Up", C_ACC, 0xFFFFFF, fm_dir != 0); draw_button(b1, "New File", C_ACC, 0xFFFFFF); draw_button(b2, "Delete", C_RED, 0xFFFFFF, fm_sel > 0);
    char path[96]; fs_path(fm_dir, path, 96); gfx::fill_rect(cx + 130, cy + 40, cw - 130, 24, 0xFFFFFF); draw_text("Path: ", cx + 138, cy + 48, C_GRAY); draw_text_n(path, cx + 186, cy + 48, C_TEXT, 50);
    int list[MAXN]; int n = fs_list(fm_dir, list, MAXN); int rows = (ch - 66) / 30;
    for (int i = 0; i < rows && i + fm_scroll < n; i++) { int idx = list[i + fm_scroll]; R r = { cx + 130, cy + 66 + i * 30, cw - 130, 30 }; if (idx == fm_sel) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xCFE0FF); else if (hov(r)) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xE8EEFA); if (fs[idx].dir) gfx::fill_round(r.x + 12, r.y + 6, 20, 16, 3, 0xF5C542); else { gfx::fill_round(r.x + 14, r.y + 5, 17, 21, 2, 0xFFFFFF); gfx::rect_outline(r.x + 14, r.y + 5, 17, 21, 0x9AA3B2); } draw_text_n(fs[idx].name, r.x + 46, r.y + 11, C_TEXT, 30); }
    if (n == 0) draw_text("(empty)", cx + 150, cy + 90, C_GRAY);
}
static void files_click(int px, int py, int cx, int cy, int cw, int ch) {
    int side_ids[5] = { 0, fs_docs, fs_pics, fs_music, -2 };
    for (int i = 0; i < 5; i++) { R r = { cx, cy + 10 + i * 34, 130, 32 }; if (hit(r, px, py)) { if (i == 4) { wins[APP_TRASH].open = true; wins[APP_TRASH].min = false; front(APP_TRASH); return; } if (side_ids[i] >= 0) { fm_dir = side_ids[i]; fm_sel = -1; fm_scroll = 0; } return; } }
    R b0 = { cx + 138, cy + 6, 80, 28 }; R b1 = { cx + 224, cy + 6, 100, 28 }; R b2 = { cx + 330, cy + 6, 80, 28 };
    if (hit(b0, px, py) && fm_dir != 0) { fm_sel = fm_dir; fm_dir = fs[fm_dir].parent; fm_scroll = 0; return; }
    if (hit(b1, px, py)) { int n = fs_new(fm_dir, "new.txt", false); fs_set_text(n, "New file"); return; }
    if (hit(b2, px, py) && fm_sel > 0) { fs[fm_sel].trashed = true; fm_sel = -1; return; }
    int list[MAXN]; int n = fs_list(fm_dir, list, MAXN); int rows = (ch - 66) / 30;
    for (int i = 0; i < rows && i + fm_scroll < n; i++) { R r = { cx + 130, cy + 66 + i * 30, cw - 130, 30 }; if (hit(r, px, py)) { int idx = list[i + fm_scroll]; fm_sel = idx; if (fs[idx].dir) { fm_dir = idx; fm_sel = -1; fm_scroll = 0; } return; } }
    fm_sel = -1;
}

// === APP TRASH ===
static void trash_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, C_BG);
    int list[MAXN]; int n = fs_trash_list(list, MAXN);
    if (tr_sel > 0 && (!fs[tr_sel].used || !fs[tr_sel].trashed)) tr_sel = -1;
    R b0 = { cx + 12, cy + 8, 110, 28 }; R b1 = { cx + 130, cy + 8, 150, 28 }; R b2 = { cx + 288, cy + 8, 130, 28 };
    draw_button(b0, "Restore", C_GREEN, 0xFFFFFF, tr_sel > 0); draw_button(b1, "Delete forever", C_RED, 0xFFFFFF, tr_sel > 0); draw_button(b2, "Empty Trash", 0x6B7280, 0xFFFFFF, n > 0);
    char st[32]; int p = itoa_(n, st); sappend(st, p, " items in Trash", 32); draw_text(st, cx + 12, cy + 48, C_GRAY);
    int rows = (ch - 70) / 30;
    for (int i = 0; i < rows && i < n; i++) { int idx = list[i]; R r = { cx, cy + 66 + i * 30, cw, 30 }; if (idx == tr_sel) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xCFE0FF); else if (hov(r)) gfx::fill_rect(r.x, r.y, r.w, r.h, 0xE8EEFA); draw_text_n(fs[idx].name, r.x + 12, r.y + 11, C_TEXT, 30); }
    if (n == 0) draw_text_c("Trash is empty", cx, cy + 80, cw, ch - 80, C_GRAY);
}
static void trash_click(int px, int py, int cx, int cy, int cw, int ch) {
    int list[MAXN]; int n = fs_trash_list(list, MAXN);
    R b0 = { cx + 12, cy + 8, 110, 28 }; R b1 = { cx + 130, cy + 8, 150, 28 }; R b2 = { cx + 288, cy + 8, 130, 28 };
    if (hit(b0, px, py) && tr_sel > 0) { fs[tr_sel].trashed = false; if (!fs_reachable(fs[tr_sel].parent)) fs[tr_sel].parent = 0; tr_sel = -1; return; }
    if (hit(b1, px, py) && tr_sel > 0) { fs_destroy(tr_sel); tr_sel = -1; return; }
    if (hit(b2, px, py) && n > 0) { for (int i = 0; i < n; i++) fs_destroy(list[i]); tr_sel = -1; return; }
    int rows = (ch - 70) / 30;
    for (int i = 0; i < rows && i < n; i++) { R r = { cx, cy + 66 + i * 30, cw, 30 }; if (hit(r, px, py)) { tr_sel = list[i]; return; } }
    tr_sel = -1;
}

// === APP CLICKER ===
static int click_gain() { return 1 + click_power_level; }
static int speed_cost() { return 20 + auto_speed_level * 10; }
static int power_cost() { return 30 * (click_power_level + 1); }
static Img skin_img(int i) {
    if (i == 1) return { img_adult_rle, img_adult_size, img_adult_w, img_adult_h };
    if (i == 2) return { img_cool_rle, img_cool_size, img_cool_w, img_cool_h };
    return { img_base_rle, img_base_size, img_base_w, img_base_h };
}
static void clicker_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_gradient(cx, cy, cw, ch, 0xFFFFFF, 0xDCE7FB);
    gfx::fill_circle(cx + 26, cy + 26, 10, 0xFFC93C); gfx::fill_circle(cx + 26, cy + 26, 6, 0xFFE08A);
    draw_int(coins, cx + 46, cy + 18, 0xB77900, 2);
    char g[24]; int p = sappend(g, 0, "+", 24); char t[8]; itoa_(click_gain(), t); p = sappend(g, p, t, 24); sappend(g, p, " / click", 24); draw_text(g, cx + 16, cy + 48, C_GRAY);
    Img im = skin_img(current_mimi);
    int sq = clicker_squish; int dh = 260 - sq, dw = im.w * dh / im.h;
    if (dw > 280) { dw = 280; dh = im.h * dw / im.w - sq; }
    int img_x = cx + 150 - dw / 2; int img_y = cy + ch - 60 - dh;
    gfx::fill_round(cx + 60, cy + ch - 70, 180, 16, 8, 0x000000, 40);
    gfx::draw_image(img_x, img_y, dw, dh, im.rle, im.size, im.w, im.h);
    int sx = cx + cw - 260; int sy = cy + 16;
    draw_text("SKINS", sx, sy, C_TEXT); sy += 20;
    struct Sk { const char* n; int id; int cost; bool owned; };
    Sk skins[3] = { {"Mini",0,0,true}, {"Teen",1,10,unlocked_teen}, {"Cool",2,25,unlocked_cool} };
    for (int i = 0; i < 3; i++) { bool sel = (current_mimi == skins[i].id); u32 bg = sel ? C_GREEN : (skins[i].owned ? 0xD1FAE5 : (coins >= skins[i].cost ? C_ACC : 0xD1D5DB)); R r = { sx, sy, 240, 32 }; gfx::fill_round(r.x, r.y, r.w, r.h, 6, bg); draw_text(skins[i].n, sx + 10, sy + 10, 0xFFFFFF); if (skins[i].owned) { if (sel) draw_text("ON", sx + 200, sy + 10, 0xFFFFFF); } else { char c[8]; itoa_(skins[i].cost, c); draw_text(c, sx + 200, sy + 10, 0xFFFFFF); } sy += 38; }
    sy += 10; draw_text("UPGRADES", sx, sy, C_TEXT); sy += 20;
    R ar = { sx, sy, 240, 32 }; gfx::fill_round(ar.x, ar.y, ar.w, ar.h, 6, has_auto_click ? 0xD1FAE5 : (coins >= 15 ? C_ACC : 0xD1D5DB)); draw_text("Auto-click", sx + 10, sy + 10, 0xFFFFFF); if (has_auto_click) draw_text("OWNED", sx + 180, sy + 10, 0x166534); else draw_text("15", sx + 200, sy + 10, 0xFFFFFF); sy += 38;
    R sr = { sx, sy, 240, 32 }; gfx::fill_round(sr.x, sr.y, sr.w, sr.h, 6, has_auto_click && coins >= speed_cost() ? C_ACC : 0xD1D5DB); draw_text("Speed +1", sx + 10, sy + 10, 0xFFFFFF); { char c[8]; itoa_(speed_cost(), c); draw_text(c, sx + 200, sy + 10, 0xFFFFFF); } sy += 38;
    R pr = { sx, sy, 240, 32 }; gfx::fill_round(pr.x, pr.y, pr.w, pr.h, 6, coins >= power_cost() ? C_ACC : 0xD1D5DB); draw_text("Power +1", sx + 10, sy + 10, 0xFFFFFF); { char c[8]; itoa_(power_cost(), c); draw_text(c, sx + 200, sy + 10, 0xFFFFFF); }
}
static void clicker_click(int px, int py, int cx, int cy, int cw, int ch) {
    Img im = skin_img(current_mimi);
    int dh = 260, dw = im.w * dh / im.h; if (dw > 280) { dw = 280; dh = im.h * dw / im.w; }
    int img_x = cx + 150 - dw / 2; int img_y = cy + ch - 60 - dh;
    if (px >= img_x && px <= img_x + dw && py >= img_y && py <= img_y + dh) { coins += click_gain(); clicker_squish = 10; beep(1200, 30); save_settings(); return; }
    int sx = cx + cw - 260; int sy = cy + 16 + 20;
    for (int i = 0; i < 3; i++) { R r = { sx, sy, 240, 32 }; if (hit(r, px, py)) { int cost = (i == 1) ? 10 : (i == 2) ? 25 : 0; bool owned = (i == 0) || (i == 1 && unlocked_teen) || (i == 2 && unlocked_cool); if (owned) { current_mimi = i; beep(1000, 30); } else if (coins >= cost) { coins -= cost; if (i == 1) unlocked_teen = true; if (i == 2) unlocked_cool = true; current_mimi = i; beep(1500, 80); } save_settings(); return; } sy += 38; }
    sy += 30;
    R ar = { sx, sy, 240, 32 }; if (hit(ar, px, py)) { if (!has_auto_click && coins >= 15) { coins -= 15; has_auto_click = true; auto_t = tnow(); beep(1500, 80); save_settings(); } return; } sy += 38;
    R sr = { sx, sy, 240, 32 }; if (hit(sr, px, py)) { if (has_auto_click && coins >= speed_cost()) { coins -= speed_cost(); auto_speed_level++; beep(1500, 80); save_settings(); } return; } sy += 38;
    R pr = { sx, sy, 240, 32 }; if (hit(pr, px, py)) { if (coins >= power_cost()) { coins -= power_cost(); click_power_level++; beep(1500, 80); save_settings(); } return; }
}

// === APP CALC ===
static int calc_disp = 0, calc_stored = 0, calc_op = 0; static bool calc_new = true;
static void calc_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, 0x1B1F2B);
    gfx::fill_round(cx + 12, cy + 12, cw - 24, 70, 8, 0x0F1218);
    char b[16]; itoa_(calc_disp, b); draw_text(b, cx + cw - 24 - text_w(b, 3), cy + 38, 0xE5E7EB, 3);
    static const char* L[16] = { "7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+" };
    int pad = 8; int bw = (cw - pad * 5) / 4; int bh = (ch - 100 - pad * 6) / 4;
    for (int i = 0; i < 16; i++) { int r = i / 4, c = i % 4; int bx = cx + pad + c * (bw + pad); int by = cy + 100 + pad + r * (bh + pad); R rr = { bx, by, bw, bh }; u32 col = 0x3A4154; if (i % 4 == 3) col = 0xF59E0B; if (i == 12) col = C_RED; if (i == 14) col = C_GREEN; if (hov(rr)) col = gfx::lerp(col, 0xFFFFFF, 1, 5); gfx::fill_round(bx, by, bw, bh, 8, col); draw_text_c(L[i], bx, by, bw, bh, 0xFFFFFF, 2); }
}
static void calc_click(int px, int py, int cx, int cy, int cw, int ch) {
    int pad = 8; int bw = (cw - pad * 5) / 4; int bh = (ch - 100 - pad * 6) / 4;
    for (int i = 0; i < 16; i++) { int r = i / 4, c = i % 4; int bx = cx + pad + c * (bw + pad); int by = cy + 100 + pad + r * (bh + pad); R rr = { bx, by, bw, bh }; if (!hit(rr, px, py)) continue; beep(1500, 20);
        static const int digits[16] = { 7,8,9,-1,4,5,6,-1,1,2,3,-1,-1,0,-1,-1 };
        if (i == 12) { calc_disp = 0; calc_stored = 0; calc_op = 0; }
        else if (i == 14) { if (calc_op == 1) calc_disp = calc_stored + calc_disp; else if (calc_op == 2) calc_disp = calc_stored - calc_disp; else if (calc_op == 3) calc_disp = calc_stored * calc_disp; else if (calc_op == 4 && calc_disp != 0) calc_disp = calc_stored / calc_disp; calc_op = 0; calc_new = true; }
        else if (i == 3 || i == 7 || i == 11 || i == 15) { calc_stored = calc_disp; calc_op = (i == 3) ? 4 : (i == 7) ? 3 : (i == 11) ? 2 : 1; calc_new = true; }
        else { int d = digits[i]; if (d >= 0) { if (calc_new) { calc_disp = d; calc_new = false; } else calc_disp = calc_disp * 10 + d; } }
        return; }
}

// === APP SETTINGS ===
static void settings_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, C_BG); int y = cy + 16;
    gfx::fill_round(cx + 12, y, cw - 24, 50, 8, 0xFFFFFF); draw_text("Keyboard Mouse", cx + 26, y + 11, C_TEXT); draw_text("Arrows / WASD", cx + 26, y + 29, C_GRAY); draw_toggle(cx + cw - 80, y + 12, use_kb_mouse); y += 58;
    gfx::fill_round(cx + 12, y, cw - 24, 50, 8, 0xFFFFFF); draw_text("HayrICons", cx + 26, y + 11, C_TEXT); draw_text("Alternative icon", cx + 26, y + 29, C_GRAY); draw_toggle(cx + cw - 80, y + 12, use_hayricon); y += 58;
    gfx::fill_round(cx + 12, y, cw - 24, 50, 8, 0xFFFFFF); draw_text("Volume", cx + 26, y + 11, C_TEXT); draw_text("0 - 100", cx + 26, y + 29, C_GRAY);
    { R m = { cx + cw - 140, y + 10, 30, 30 }; R p = { cx + cw - 70, y + 10, 30, 30 }; draw_button(m, "-", C_ACC, 0xFFFFFF); draw_button(p, "+", C_ACC, 0xFFFFFF); char vb[8]; itoa_(volume, vb); draw_text_c(vb, cx + cw - 110, y + 14, 40, 22, C_TEXT); } y += 58;
    gfx::fill_round(cx + 12, y, cw - 24, 50, 8, 0xFFFFFF); draw_text("Mouse speed", cx + 26, y + 11, C_TEXT); draw_text("1=slow 4=fast", cx + 26, y + 29, C_GRAY);
    { R m = { cx + cw - 140, y + 10, 30, 30 }; R p = { cx + cw - 70, y + 10, 30, 30 }; draw_button(m, "-", C_ACC, 0xFFFFFF); draw_button(p, "+", C_ACC, 0xFFFFFF); char mb[8]; itoa_(mouse_speed, mb); draw_text_c(mb, cx + cw - 110, y + 14, 40, 22, C_TEXT); }
    draw_text("Saved automatically", cx + 16, cy + ch - 20, C_GRAY);
}
static void settings_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)ch; int y = cy + 16;
    if (hit({ cx + 12, y, cw - 24, 50 }, px, py)) { use_kb_mouse = !use_kb_mouse; save_settings(); return; } y += 58;
    if (hit({ cx + 12, y, cw - 24, 50 }, px, py)) { use_hayricon = !use_hayricon; save_settings(); return; } y += 58;
    if (hit({ cx + cw - 140, y + 10, 30, 30 }, px, py)) { if (volume >= 10) volume -= 10; save_settings(); return; }
    if (hit({ cx + cw - 70, y + 10, 30, 30 }, px, py)) { if (volume <= 90) volume += 10; save_settings(); return; } y += 58;
    if (hit({ cx + cw - 140, y + 10, 30, 30 }, px, py)) { if (mouse_speed > 1) mouse_speed--; save_settings(); return; }
    if (hit({ cx + cw - 70, y + 10, 30, 30 }, px, py)) { if (mouse_speed < 4) mouse_speed++; save_settings(); return; }
}

// === APP DISPATCH ===
static void app_draw(int a, int cx, int cy, int cw, int ch) {
    switch (a) { case APP_FILES: files_draw(cx, cy, cw, ch); break; case APP_TRASH: trash_draw(cx, cy, cw, ch); break; case APP_CLICK: clicker_draw(cx, cy, cw, ch); break; case APP_CALC: calc_draw(cx, cy, cw, ch); break; case APP_SET: settings_draw(cx, cy, cw, ch); break; case APP_BROWSER: browser_draw(cx, cy, cw, ch); break; case APP_GALLERY: gallery_draw(cx, cy, cw, ch); break; }
}
static void app_click(int a, int px, int py, int cx, int cy, int cw, int ch) {
    switch (a) { case APP_FILES: files_click(px, py, cx, cy, cw, ch); break; case APP_TRASH: trash_click(px, py, cx, cy, cw, ch); break; case APP_CLICK: clicker_click(px, py, cx, cy, cw, ch); break; case APP_CALC: calc_click(px, py, cx, cy, cw, ch); break; case APP_SET: settings_click(px, py, cx, cy, cw, ch); break; case APP_BROWSER: browser_click(px, py, cx, cy, cw, ch); break; case APP_GALLERY: gallery_click(px, py, cx, cy, cw, ch); break; }
}

// === ICONS ===
static void draw_app_icon(int a, int x, int y) {
    if (a == APP_FILES) { gfx::fill_round(x + 4, y + 8, 24, 12, 3, 0xE0A800); gfx::fill_round(x + 2, y + 15, 52, 38, 6, 0xF5C542); gfx::fill_round(x + 2, y + 24, 52, 29, 6, 0xFFD75E); }
    else if (a == APP_TRASH) { bool full = fs_trash_count() > 0; if (full) { gfx::fill_rect(x + 14, y + 0, 16, 12, 0xFFFFFF); gfx::fill_rect(x + 28, y + 2, 14, 11, 0xFDE68A); } gfx::fill_round(x + 8, y + 10, 40, 7, 2, 0x9AA3B2); gfx::fill_rect(x + 22, y + 6, 12, 5, 0x9AA3B2); gfx::fill_round(x + 11, y + 18, 34, 36, 5, 0xB8C0CC); for (int k = 0; k < 3; k++) gfx::fill_rect(x + 19 + k * 8, y + 25, 3, 22, 0x7C8696); }
    else if (a == APP_CLICK) { Img im = use_hayricon ? Img{img_hayrulla_rle, img_hayrulla_size, img_hayrulla_w, img_hayrulla_h} : Img{img_base_rle, img_base_size, img_base_w, img_base_h}; int dh = 52, dw = im.w * dh / im.h; if (dw > 52) { dw = 52; dh = im.h * dw / im.w; } gfx::fill_round(x, y, 56, 56, 12, 0xFFFFFF); gfx::draw_image(x + 28 - dw / 2, y + 28 - dh / 2, dw, dh, im.rle, im.size, im.w, im.h); }
    else if (a == APP_CALC) { gfx::fill_round(x + 6, y + 1, 44, 54, 7, 0x2D3340); gfx::fill_round(x + 11, y + 6, 34, 14, 3, 0x9BE7A7); for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) gfx::fill_round(x + 11 + c * 9, y + 25 + r * 7, 7, 5, 1, c == 3 ? 0xF59E0B : 0xE5E7EB); }
    else if (a == APP_BROWSER) { gfx::fill_circle(x + 28, y + 28, 26, 0x3B82F6); gfx::fill_circle(x + 28, y + 28, 22, 0x60A5FA); gfx::fill_rect(x + 27, y + 6, 2, 44, 0xFFFFFF); gfx::fill_rect(x + 6, y + 27, 44, 2, 0xFFFFFF); gfx::fill_circle(x + 28, y + 28, 10, 0xFFFFFF); }
    else if (a == APP_GALLERY) { gfx::fill_round(x + 2, y + 5, 52, 46, 6, 0xDCE7F5); gfx::fill_round(x + 6, y + 9, 44, 38, 4, 0xFFFFFF); gfx::fill_circle(x + 39, y + 19, 5, 0xF5C542); gfx::fill_rect(x + 10, y + 33, 14, 10, 0x60A5FA); gfx::fill_rect(x + 24, y + 27, 20, 16, 0x22C55E); }
    else { int cx = x + 28, cy = y + 28; static const int dx[8] = { 22, 15, 0, -15, -22, -15, 0, 15 }, dy[8] = { 0, 15, 22, 15, 0, -15, -22, -15 }; for (int k = 0; k < 8; k++) gfx::fill_round(cx + dx[k] - 5, cy + dy[k] - 5, 10, 10, 3, 0xAEB6C4); gfx::fill_circle(cx, cy, 18, 0xAEB6C4); gfx::fill_circle(cx, cy, 8, 0x4B5563); }
}

static const int desk_apps[7] = { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET, APP_BROWSER, APP_GALLERY };
static R desk_icon(int i) { return i < 6 ? R{ 18, 18 + i * 82, 88, 78 } : R{ 112, 18, 88, 78 }; }
static int desk_sel = -1;

// === RENDER ===
static void draw_wallpaper() { gfx::fill_gradient(0, 0, SCRW, SCRH, 0x0E1A3F, 0x4A2A6B); gfx::fill_circle(SCRW * 3 / 4, SCRH / 3, SCRH / 3, 0x5B7CFA, 40); gfx::fill_circle(SCRW / 3, SCRH * 3 / 4, SCRH / 4, 0xFF6FB5, 34); }
static void draw_desktop_icons() {
    for (int i = 0; i < 7; i++) { R r = desk_icon(i); if (desk_sel == i) gfx::fill_round(r.x, r.y, r.w, r.h, 10, 0xFFFFFF, 70); else if (hov(r)) gfx::fill_round(r.x, r.y, r.w, r.h, 10, 0xFFFFFF, 34); draw_app_icon(desk_apps[i], r.x + 16, r.y + 6); const char* t = app_title[desk_apps[i]]; int tw = text_w(t); if (tw > r.w) tw = r.w; draw_text(t, r.x + (r.w - text_w(t)) / 2 + 1, r.y + 71, 0x000000); draw_text(t, r.x + (r.w - text_w(t)) / 2, r.y + 70, 0xFFFFFF); }
}
static R win_close_btn(const Win& w) { return { w.x + w.w - 30, w.y + 4, 24, 22 }; }
static R win_min_btn(const Win& w) { return { w.x + w.w - 58, w.y + 4, 24, 22 }; }
static void draw_window(int a) {
    Win& w = wins[a]; bool active = (top_win() == a);
    gfx::fill_round(w.x + 4, w.y + 6, w.w, w.h, 10, 0x000000, 70);
    gfx::fill_round(w.x, w.y, w.w, TITLEH + 12, 10, active ? 0x2D4A9A : 0x353A50);
    gfx::fill_gradient(w.x + 4, w.y + 2, w.w - 8, TITLEH / 2, active ? 0x3C5DB8 : 0x40465F, active ? 0x2D4A9A : 0x353A50);
    draw_text(app_title[a], w.x + 14, w.y + 11, 0xFFFFFF);
    R m = win_min_btn(w), c = win_close_btn(w);
    gfx::fill_round(m.x, m.y, m.w, m.h, 5, hov(m) ? 0x6B7BB5 : 0x4A5A94); draw_text_c("-", m.x, m.y, m.w, m.h, 0xFFFFFF);
    gfx::fill_round(c.x, c.y, c.w, c.h, 5, hov(c) ? C_RED : 0x4A5A94); draw_text_c("x", c.x, c.y, c.w, c.h, 0xFFFFFF);
    app_draw(a, w.x, w.y + TITLEH, w.w, w.h - TITLEH);
    gfx::rect_outline(w.x, w.y + TITLEH, w.w, w.h - TITLEH, active ? 0x2D4A9A : 0x353A50);
}
static R start_btn() { return { 6, SCRH - TASKBAR_H + 6, 96, 32 }; }
static R taskbar_btn(int slot) { return { 112 + slot * 148, SCRH - TASKBAR_H + 6, 142, 32 }; }
static void draw_taskbar() {
    int ty = SCRH - TASKBAR_H; gfx::fill_rect_a(0, ty, SCRW, TASKBAR_H, 0x10142A, 215); gfx::fill_rect(0, ty, SCRW, 1, 0x4C5C9A);
    R s = start_btn(); gfx::fill_round(s.x, s.y, s.w, s.h, 8, start_open ? C_ACC_D : (hov(s) ? 0x5B9BFF : C_ACC)); draw_text_c("iMimi", s.x, s.y, s.w, s.h, 0xFFFFFF);
    int slot = 0; int tw = top_win();
    for (int a = 0; a < NAPPS; a++) if (wins[a].open) { R r = taskbar_btn(slot++); bool act = (tw == a); gfx::fill_round(r.x, r.y, r.w, r.h, 8, act ? 0x3D5CB8 : (hov(r) ? 0x2B3566 : 0x1F2748)); if (act) gfx::fill_rect(r.x + 10, r.y + r.h - 3, r.w - 20, 2, 0x8FB4FF); draw_text_n(app_title[a], r.x + 10, r.y + 12, wins[a].min ? 0x8892B8 : 0xFFFFFF, 16); }
    char c[8]; c[0] = '0' + clock_h / 10; c[1] = '0' + clock_h % 10; c[2] = ':'; c[3] = '0' + clock_m / 10; c[4] = '0' + clock_m % 10; c[5] = 0; draw_text(c, SCRW - 16 - text_w(c, 2), ty + 14, 0xFFFFFF, 2);
    char v[16]; int p = sappend(v, 0, "VOL ", 16); char t[8]; itoa_(volume, t); sappend(v, p, t, 16); draw_text(v, SCRW - 16 - text_w(c, 2) - 24 - text_w(v), ty + 18, 0xB4BCE0);
}
static R pw_box() { return { (SCRW - 380) / 2, (SCRH - 190) / 2, 380, 190 }; }
static R pw_btn(int i) { R b = pw_box(); if (i == 2) return { b.x + 100, b.y + 130, 180, 36 }; return { b.x + 20 + i * 180, b.y + 70, 160, 44 }; }
static void draw_power() {
    gfx::fill_rect_a(0, 0, SCRW, SCRH, 0x000000, 130); R b = pw_box();
    gfx::fill_round(b.x, b.y, b.w, b.h, 12, 0x181D38); draw_text_c("Power", b.x, b.y + 14, b.w, 16, 0xFFFFFF, 2);
    draw_button(pw_btn(0), "Restart", 0xF59E0B, 0x000000); draw_button(pw_btn(1), "Shut down", C_RED, 0xFFFFFF); draw_button(pw_btn(2), "Cancel", 0x4B5563, 0xFFFFFF);
}

// === КУРСОР (как в Windows/macOS) ===
static void draw_cursor(int x, int y) {
    // 12x19 bitmap: 1=чёрный контур, 2=белая заливка, 0=прозрачный
    static const u8 arrow[19][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0},
        {1,2,2,2,2,1,1,1,1,1,1,1},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,1,2,1,0,0,0,0,0,0,0},
        {1,1,0,1,2,1,0,0,0,0,0,0},
        {0,0,0,0,1,2,1,0,0,0,0,0},
        {0,0,0,0,1,2,1,0,0,0,0,0},
        {0,0,0,0,0,1,1,0,0,0,0,0},
        {0,0,0,0,0,1,0,0,0,0,0,0}
    };
    for (int j = 0; j < 19; j++) {
        for (int i = 0; i < 12; i++) {
            u8 v = arrow[j][i];
            if (v == 1) gfx::put_pixel(x + i, y + j, 0x000000);
            else if (v == 2) gfx::put_pixel(x + i, y + j, 0xFFFFFF);
        }
    }
}

static void render() {
    draw_wallpaper(); draw_desktop_icons();
    for (int i = 0; i < NAPPS; i++) { int a = zord[i]; if (wins[a].open && !wins[a].min) draw_window(a); }
    draw_taskbar();
    if (power_open) draw_power();
    draw_cursor(mouse::x, mouse::y);
}

// === INPUT ===
static void open_app(int a) { if (!wins[a].open) wins[a].open = true; wins[a].min = false; front(a); beep(1000, 40); }
static void close_app(int a) { wins[a].open = false; wins[a].min = false; beep(700, 40); }
static void handle_press(int px, int py) {
    dirty = true;
    if (power_open) { if (hit(pw_btn(0), px, py)) reboot(); else if (hit(pw_btn(1), px, py)) shutdown(); else power_open = false; return; }
    if (py >= SCRH - TASKBAR_H) {
        if (hit(start_btn(), px, py)) { start_open = !start_open; return; }
        int slot = 0;
        for (int a = 0; a < NAPPS; a++) if (wins[a].open) { if (hit(taskbar_btn(slot++), px, py)) { if (wins[a].min) { wins[a].min = false; front(a); } else if (top_win() == a) wins[a].min = true; else front(a); return; } }
        return;
    }
    int a = win_at(px, py);
    if (a >= 0) {
        front(a); Win& w = wins[a];
        if (py < w.y + TITLEH) { if (hit(win_close_btn(w), px, py)) { close_app(a); return; } if (hit(win_min_btn(w), px, py)) { w.min = true; return; } drag_win = a; drag_dx = px - w.x; drag_dy = py - w.y; return; }
        app_click(a, px, py, w.x, w.y + TITLEH, w.w, w.h - TITLEH); return;
    }
    for (int i = 0; i < 7; i++) if (hit(desk_icon(i), px, py)) { desk_sel = i; open_app(desk_apps[i]); return; }
    desk_sel = -1;
}
static void handle_release() { drag_win = -1; dirty = true; }
static void on_key(int k) {
    dirty = true;
    if (k == K_F1) { power_open = !power_open; start_open = false; return; }
    if (k == K_F2) { if (volume < 100) volume += 10; save_settings(); beep(900, 30); return; }
    if (k == K_F3) { if (volume > 0) volume -= 10; save_settings(); beep(700, 30); return; }
    if (power_open) { if (k == 27) power_open = false; return; }
    int t = top_win();
    if (t == APP_CALC) {
        if (k >= '0' && k <= '9') { if (calc_new) { calc_disp = k - '0'; calc_new = false; } else calc_disp = calc_disp * 10 + (k - '0'); return; }
        if (k == '+' || k == '-' || k == '*' || k == '/') { calc_stored = calc_disp; calc_op = (k == '+') ? 1 : (k == '-') ? 2 : (k == '*') ? 3 : 4; calc_new = true; return; }
        if (k == '=' || k == '\n') { if (calc_op == 1) calc_disp = calc_stored + calc_disp; else if (calc_op == 2) calc_disp = calc_stored - calc_disp; else if (calc_op == 3) calc_disp = calc_stored * calc_disp; else if (calc_op == 4 && calc_disp != 0) calc_disp = calc_stored / calc_disp; calc_op = 0; calc_new = true; return; }
    }
    if (t == APP_BROWSER) {
        if (web_editing) {
            if (k == 27) { web_editing = false; return; }
            if (k == '\n') { browser_load(); return; }
            int n = slen(web_url);
            if (k == '\b') { if (n > 0) web_url[n - 1] = 0; return; }
            if (k >= 32 && k <= 126 && n < (int)sizeof(web_url) - 1) {
                web_url[n] = (char)k; web_url[n + 1] = 0; return;
            }
            return;
        }
        if (k == '\n') { browser_load(); return; }
        if (k == K_UP) { if (web_scroll > 0) web_scroll--; return; }
        if (k == K_DOWN) { web_scroll++; return; }
        if (k == 'r' || k == 'R') { browser_load(); return; }
    }
    if (t == APP_GALLERY && gallery_selected >= 0) {
        if (k == K_LEFT) { gallery_selected = (gallery_selected + gallery_image_count - 1) % gallery_image_count; return; }
        if (k == K_RIGHT) { gallery_selected = (gallery_selected + 1) % gallery_image_count; return; }
        if (k == 27) { gallery_selected = -1; return; }
    }
    if (k == 27) { if (t >= 0) close_app(t); return; }
    if (!use_kb_mouse) return;
    int step = 12 * mouse_speed;
    if (k == K_UP || k == 'w') mouse::y -= step;
    if (k == K_DOWN || k == 's') mouse::y += step;
    if (k == K_LEFT || k == 'a') mouse::x -= step;
    if (k == K_RIGHT || k == 'd') mouse::x += step;
    if (mouse::x < 0) mouse::x = 0; if (mouse::x > SCRW - 1) mouse::x = SCRW - 1;
    if (mouse::y < 0) mouse::y = 0; if (mouse::y > SCRH - 1) mouse::y = SCRH - 1;
    if (k == ' ' || k == '\n') { handle_press(mouse::x, mouse::y); handle_release(); }
}

// === SPLASH ===
static void splash(int stage) {
    u32 bg = use_hayricon ? 0x121212 : 0xFFFFFF, fg = use_hayricon ? 0xFFFFFF : 0x1F2937;
    gfx::fill_rect(0, 0, SCRW, SCRH, bg);
    Img im = use_hayricon ? Img{img_hayrulla_rle, img_hayrulla_size, img_hayrulla_w, img_hayrulla_h} : Img{img_base_rle, img_base_size, img_base_w, img_base_h};
    int dh = 240, dw = im.w * dh / im.h;
    gfx::draw_image(SCRW / 2 - dw / 2, SCRH / 2 - 190, dw, dh, im.rle, im.size, im.w, im.h);
    draw_text_c("iMimi OS", 0, SCRH / 2 + 70, SCRW, 24, fg, 3);
    draw_text_c("PC Edition", 0, SCRH / 2 + 104, SCRW, 8, 0x888888);
    int bw = 300, bx = SCRW / 2 - bw / 2, by = SCRH / 2 + 140;
    gfx::fill_round(bx, by, bw, 8, 4, use_hayricon ? 0x333333 : 0xE5E7EB);
    gfx::fill_round(bx, by, bw * (stage + 1) / 3, 8, 4, C_ACC);
    gfx::swap();
}

// === KMAIN ===
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic; gfx::init(mb);
    if (!gfx::ok) { while (1) asm volatile("cli; hlt"); }
    mouse::init(); load_settings(); fs_init(); init_windows();
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80); outb(0x3F8 + 0, 0x03); outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7); outb(0x3F8 + 4, 0x0B);
    splash(0);
    { u8 last = cmos_read(0); int changes = 0; u32 t0 = 0;
      for (u32 g = 0; g < 30000000u && changes < 2; g++) { u8 s = cmos_read(0); if (s != last) { last = s; changes++; if (changes == 1) { t0 = tnow(); splash(1); } else { u32 pm = (tnow() - t0) / 1000; if (pm > 1000 && pm < 5000000) tsc16_per_ms = pm; splash(2); } } } }
    render(); gfx::swap();
    u32 last_clock = 0;
    while (1) {
        bool need_render = dirty; dirty = false;
        while (inb(0x64) & 1) { u8 b = inb(0x60); if (b & 0x20) mouse::feed(b); else kbd::feed(b); need_render = true; }
        if (mouse::ev_press) { mouse::ev_press = false; handle_press(mouse::px, mouse::py); need_render = true; }
        if (mouse::ev_release) { mouse::ev_release = false; handle_release(); need_render = true; }
        if (drag_win >= 0) { wins[drag_win].x = mouse::x - drag_dx; wins[drag_win].y = mouse::y - drag_dy; if (wins[drag_win].x < 0) wins[drag_win].x = 0; if (wins[drag_win].y < 0) wins[drag_win].y = 0; need_render = true; }
        if (snd_on && (s32)(tnow() - snd_end) >= 0) stop_sound();
        u32 now = ms_now(); blink_state = ((now / 500) % 2) == 0;
        if (clicker_squish > 0) { clicker_squish--; need_render = true; }
        u32 ck = now / 1000;
        if (ck != last_clock) { last_clock = ck; rtc_time(clock_h, clock_m); need_render = true; }
        if (has_auto_click) { u32 speed_ms = 1000 - auto_speed_level * 60; if (speed_ms < 100) speed_ms = 100; if (now - auto_t >= speed_ms) { auto_t = now; coins += 1 + click_power_level; if (wins[APP_CLICK].open) need_render = true; } }
        if (need_render) { render(); gfx::swap(); } else { asm volatile("pause"); }
    }
}