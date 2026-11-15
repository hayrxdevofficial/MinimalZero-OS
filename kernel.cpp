// ============================================================
//  iMimi OS — PC Edition + REAL TEXT BROWSER
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

// === ГЛОБАЛКИ ===
static u32 tsc16_per_ms = 100000;
static inline u32 tnow() { u32 lo, hi; asm volatile("rdtsc" : "=a"(lo), "=d"(hi)); return (hi << 28) | (lo >> 4); }
static inline u32 ms_now() { return tnow() / tsc16_per_ms; }
static u8 cmos_read(u8 reg) { outb(0x70, reg); return inb(0x71); }
static void cmos_write(u8 reg, u8 val) { outb(0x70, (reg | 0x80)); outb(0x71, val); outb(0x70, 0); }

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

// === ГРАФИКА (сокращена, как в прошлый раз) ===
#define MAXW 1280
#define MAXH 1024
namespace gfx {
    static u8* fb = nullptr; static u32 W = 1024, H = 768, pitch = 0; static u8 bpp = 32; static bool ok = false; static u32 back[MAXW * MAXH];
    void init(multiboot_info* mb) { if ((mb->flags & (1u << 12)) && mb->framebuffer_addr) { fb = (u8*)(u32)mb->framebuffer_addr; W = mb->framebuffer_width; H = mb->framebuffer_height; pitch = mb->framebuffer_pitch; bpp = mb->framebuffer_bpp; if (W > MAXW) W = MAXW; if (H > MAXH) H = MAXH; ok = 1; } }
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
    void fill_round(int x, int y, int w, int h, int r, u32 c, u32 a = 256) { if (r * 2 > h) r = h / 2; if (r * 2 > w) r = w / 2; for (int j = 0; j < h; j++) { int dy = j < h - 1 - j ? j : h - 1 - j; int ins = dy < r ? corner_inset(r, dy) : 0; if (a >= 256) fill_rect(x + ins, y + j, w - 2 * ins, 1, c); else fill_rect_a(x + ins, y + j, w - 2 * ins, 1, c, a); } }
    void fill_circle(int cx, int cy, int r, u32 c) { for (int j = -r; j <= r; j++) { int half = isqrt_(r * r - j * j); fill_rect(cx - half, cy + j, half * 2 + 1, 1, c); } }
    static inline u32 rgb565_to_888(u16 c) { return (((c >> 11) & 0x1F) * 255 / 31 << 16) | (((c >> 5) & 0x3F) * 255 / 63 << 8) | ((c & 0x1F) * 255 / 31); }
    void draw_image(int ox, int oy, int dw, int dh, const u16* rle, u32 size, int iw, int ih) { u32 idx = 0; int x = 0, y = 0; while (idx < size && y < ih) { u16 hd = rle[idx++]; u32 len = hd & 0x7FFF; bool run = (hd & 0x8000) != 0; u32 col = 0; if (run) col = rgb565_to_888(rle[idx++]); for (u32 i = 0; i < len && y < ih; i++) { if (!run) col = rgb565_to_888(rle[idx++]); int x0 = ox + x * dw / iw, x1 = ox + (x + 1) * dw / iw; int y0 = oy + y * dh / ih, y1 = oy + (y + 1) * dh / ih; fill_rect(x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1, col); if (++x >= iw) { x = 0; y++; } } } }
    void swap() { if (!fb) return; if (bpp == 32) { u32 p32 = pitch / 4; for (u32 y = 0; y < H; y++) { u32* d = (u32*)fb + y * p32; u32* s = &back[y * W]; for (u32 x = 0; x < W; x++) d[x] = s[x]; } } else { for (u32 y = 0; y < H; y++) { u8* d = fb + y * pitch; u32* s = &back[y * W]; for (u32 x = 0; x < W; x++) { u32 c = s[x]; d[x*3] = c & 255; d[x*3+1] = (c >> 8) & 255; d[x*3+2] = (c >> 16) & 255; } } } }
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
    if (c < 32 || c > 126) c = '?';
    const u8* g = font8x8[c - 32];
    for (int r = 0; r < 8; r++) for (int cl = 0; cl < 8; cl++)
        if (g[r] & (1 << (7 - cl))) { if (sc == 1) gfx::put_pixel(x + cl, y + r, color); else gfx::fill_rect(x + cl * sc, y + r * sc, sc, sc, color); }
}
static void draw_text(const char* s, int x, int y, u32 color, int sc = 1) { while (*s) { draw_char(*s++, x, y, color, sc); x += 8 * sc; } }
static void draw_text_n(const char* s, int x, int y, u32 color, int maxc) { int n = 0; while (*s && n < maxc) { draw_char(*s++, x, y, color); x += 8; n++; } }
static int  text_w(const char* s, int sc = 1) { return slen(s) * 8 * sc; }
static void draw_text_c(const char* s, int x, int y, int w, int h, u32 color, int sc = 1) { draw_text(s, x + (w - text_w(s, sc)) / 2, y + (h - 8 * sc) / 2, color, sc); }
static void draw_int(int v, int x, int y, u32 color, int sc = 1) { char b[16]; itoa_(v, b); draw_text(b, x, y, color, sc); }

// === МЫШЬ, КЛАВИАТУРА, ЗВУК, НАСТРОЙКИ (сокращено) ===
static bool use_kb_mouse = true, use_hayricon = false;
static int  mouse_speed = 1, volume = 50;
static int  coins = 0;
static bool unlocked_teen = false, unlocked_cool = false, has_auto_click = false;
static int  auto_speed_level = 0, click_power_level = 0;
static bool dirty = true;
static int  clock_h = 12, clock_m = 0;
static int  current_mimi = 0;
static int  clicker_squish = 0;
static u32  auto_t = 0;

static bool snd_on = false; static u32 snd_end = 0;
static void stop_sound() { outb(0x61, inb(0x61) & 0xFC); snd_on = false; }
static void beep(u32 freq, u32 ms) {
    if (volume == 0 || freq < 20) return;
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6); outb(0x42, (u8)div); outb(0x42, (u8)(div >> 8));
    outb(0x61, inb(0x61) | 3);
    snd_on = true; snd_end = tnow() + ms * tsc16_per_ms;
}

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
static void reboot() { save_settings(); outb(0x64, 0xFE); while (1) asm volatile("cli; hlt"); }
static void shutdown() { save_settings(); outw(0x604, 0x2000); while (1) asm volatile("cli; hlt"); }
static void rtc_time(int& h, int& m) {
    for (int i = 0; i < 100000 && (cmos_read(0x0A) & 0x80); i++);
    u8 hh = cmos_read(4), mm = cmos_read(2), b = cmos_read(0x0B);
    bool pm = (hh & 0x80) != 0; hh &= 0x7F;
    if (!(b & 4)) { hh = (hh & 0x0F) + (hh >> 4) * 10; mm = (mm & 0x0F) + (mm >> 4) * 10; }
    if (!(b & 2)) { if (pm && hh < 12) hh += 12; if (!pm && hh == 12) hh = 0; }
    h = hh % 24; m = mm % 60;
}

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

// === ПАЛИТРА И ХЕЛПЕРЫ ===
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

// === ФАЙЛОВАЯ СИСТЕМА (сокращена) ===
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
enum { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET, APP_BROWSER, NAPPS };
static const char* app_title[NAPPS] = { "Files", "Trash", "Mimi Clicker", "Calculator", "Settings", "Browser" };
struct Win { bool open, min; int x, y, w, h; };
static Win wins[NAPPS];
static int zord[NAPPS];
static int drag_win = -1, drag_dx = 0, drag_dy = 0;
static bool power_open = false;
static void init_windows() {
    static const int sz[NAPPS][2] = { {640,460},{540,400},{580,440},{300,410},{460,400},{600,450} };
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

// === СЕТЬ ===
#define COM1 0x3F8
static void net_init() { outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80); outb(COM1 + 0, 0x03); outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B); }
static void net_write(u8 b) { int t = 100000; while (!(inb(COM1 + 5) & 0x20) && --t); outb(COM1, b); }
static int net_read() { if (!(inb(COM1 + 5) & 1)) return -1; return inb(COM1); }
static void net_get_html(const char* url, char* out, int max_len) {
    out[0] = 0;
    // Команда 0x01: "Дай HTML для этого URL"
    outb(0xE9, 0x01);
    for (int i = 0; url[i]; i++) outb(0xE9, (u8)url[i]);
    outb(0xE9, 0);
    
    // Читаем ответ
    int i = 0; u32 timeout = 0;
    while (i < max_len - 1) {
        int b = net_read();
        if (b >= 0) { out[i++] = (char)b; timeout = 0; }
        else { timeout++; if (timeout > 50000000) break; }
    }
    out[i] = 0;
}

// === БРАУЗЕР ===
static char web_url[128] = "info.cern.ch";
static char web_buffer[4096];
static bool web_loading = false;
static int web_scroll = 0;

static void strip_html(const char* html, char* out, int max_len) {
    int in_tag = 0, out_i = 0;
    for (int i = 0; html[i] && out_i < max_len - 1; i++) {
        if (html[i] == '<') { in_tag = 1; continue; }
        if (html[i] == '>') { in_tag = 0; continue; }
        if (!in_tag) {
            if (html[i] == '\n' || html[i] == '\r') { if (out_i > 0 && out[out_i-1] != '\n') out[out_i++] = '\n'; }
            else out[out_i++] = html[i];
        }
    }
    out[out_i] = 0;
}

static void browser_draw(int cx, int cy, int cw, int ch) {
    gfx::fill_rect(cx, cy, cw, ch, 0xFFFFFF);
    gfx::fill_rect(cx, cy, cw, 40, 0xE5E7EB);
    draw_text_n(web_url, cx + 16, cy + 12, 0x1F2937, 50);
    
    if (web_loading) {
        draw_text_c("Loading...", cx, cy + 60, cw, 40, 0x3B82F6, 2);
        return;
    }

    // Парсим HTML (убираем теги)
    char clean_text[4096];
    strip_html(web_buffer, clean_text, 4096);

    // Рисуем текст
    int tx = cx + 16, ty = cy + 50;
    int line_h = 12;
    int start_line = web_scroll;
    int curr_line = 0;
    
    for (int i = 0; clean_text[i]; i++) {
        if (curr_line >= start_line) {
            if (clean_text[i] == '\n') { tx = cx + 16; ty += line_h; }
            else {
                if (tx > cx + cw - 20) { tx = cx + 16; ty += line_h; }
                if (ty > cy + ch - 20) break; // Конец экрана
                draw_char(clean_text[i], tx, ty, 0x1F2937, 1);
                tx += 8;
            }
        }
        if (clean_text[i] == '\n') curr_line++;
    }
}

static void browser_click(int px, int py, int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    R b1 = { cx + 16, cy + 60, 100, 30 }; // Кнопка "Загрузить"
    // В реальности тут нужна кнопка "Go", а пока просто клик по экрану запускает загрузку
    (void)b1;
    (void)px; (void)py;
}

// Запускаем загрузку по нажатию Enter в браузере
static void browser_load() {
    web_loading = true;
    net_get_html(web_url, web_buffer, 4096);
    web_loading = false;
}

// === ОТРИСОВКА ПРИЛОЖЕНИЙ ===
static void draw_app_icon(int a, int x, int y) {
    if (a == APP_FILES) { gfx::fill_round(x + 4, y + 8, 24, 12, 3, 0xE0A800); gfx::fill_round(x + 2, y + 15, 52, 38, 6, 0xF5C542); }
    else if (a == APP_TRASH) { gfx::fill_round(x + 11, y + 18, 34, 36, 5, 0xB8C0CC); gfx::fill_rect(x + 22, y + 6, 12, 5, 0x9AA3B2); }
    else if (a == APP_CLICK) { gfx::fill_round(x, y, 56, 56, 12, 0xFFFFFF); int dh = 52, dw = img_base_w * dh / img_base_h; gfx::draw_image(x + 28 - dw/2, y + 28 - dh/2, dw, dh, img_base_rle, img_base_size, img_base_w, img_base_h); }
    else if (a == APP_CALC) { gfx::fill_round(x + 6, y + 1, 44, 54, 7, 0x2D3340); gfx::fill_round(x + 11, y + 6, 34, 14, 3, 0x9BE7A7); }
    else if (a == APP_BROWSER) { gfx::fill_circle(x + 28, y + 28, 26, 0x3B82F6); gfx::fill_circle(x + 28, y + 28, 22, 0x60A5FA); gfx::fill_rect(x + 27, y + 6, 2, 44, 0xFFFFFF); gfx::fill_rect(x + 6, y + 27, 44, 2, 0xFFFFFF); }
    else { gfx::fill_circle(x + 28, y + 28, 20, 0xAEB6C4); }
}

static const int desk_apps[6] = { APP_FILES, APP_TRASH, APP_CLICK, APP_CALC, APP_SET, APP_BROWSER };
static R desk_icon(int i) { return { 18, 18 + i * 82, 88, 78 }; }

static void app_draw(int a, int cx, int cy, int cw, int ch) {
    if (a == APP_FILES) {
        gfx::fill_rect(cx, cy, cw, ch, C_BG);
        int n = fs_list(fm_dir, (int*)web_buffer, 10); // Используем буфер для демо
        draw_text("Files (Demo)", cx + 20, cy + 20, C_TEXT);
    } else if (a == APP_CLICK) {
        gfx::fill_gradient(cx, cy, cw, ch, 0xFFFFFF, 0xDCE7FB);
        draw_int(coins, cx + 20, cy + 20, 0xB77900, 3);
        int dh = 200, dw = img_base_w * dh / img_base_h;
        gfx::draw_image(cx + cw/2 - dw/2, cy + ch/2 - dh/2, dw, dh, img_base_rle, img_base_size, img_base_w, img_base_h);
    } else if (a == APP_CALC) {
        gfx::fill_rect(cx, cy, cw, ch, 0x1B1F2B);
        draw_int(coins, cx + 20, cy + 20, 0xFFFFFF, 2);
    } else if (a == APP_BROWSER) {
        browser_draw(cx, cy, cw, ch);
    } else {
        gfx::fill_rect(cx, cy, cw, ch, C_BG);
        draw_text(app_title[a], cx + 20, cy + 20, C_TEXT);
    }
}

static void app_click(int a, int px, int py, int cx, int cy, int cw, int ch) {
    if (a == APP_CLICK) {
        int dh = 200, dw = img_base_w * dh / img_base_h;
        int ix = cx + cw/2 - dw/2, iy = cy + ch/2 - dh/2;
        if (px >= ix && px <= ix+dw && py >= iy && py <= iy+dh) { coins++; clicker_squish = 10; beep(1200, 20); }
    } else if (a == APP_BROWSER) {
        browser_click(px, py, cx, cy, cw, ch);
    }
}

// === RENDER ===
static void draw_wallpaper() { gfx::fill_gradient(0, 0, SCRW, SCRH, 0x0E1A3F, 0x4A2A6B); gfx::fill_circle(SCRW * 3 / 4, SCRH / 3, 200, 0x5B7CFA); }
static void render() {
    draw_wallpaper();
    for (int i = 0; i < 6; i++) {
        R r = desk_icon(i);
        if (hov(r)) gfx::fill_round(r.x, r.y, r.w, r.h, 10, 0xFFFFFF, 50);
        draw_app_icon(desk_apps[i], r.x + 16, r.y + 6);
        const char* t = app_title[desk_apps[i]];
        draw_text(t, r.x + (r.w - text_w(t))/2, r.y + 70, 0xFFFFFF);
    }
    for (int i = 0; i < NAPPS; i++) {
        int a = zord[i]; if (wins[a].open && !wins[a].min) {
            Win& w = wins[a]; bool active = (top_win() == a);
            gfx::fill_round(w.x + 4, w.y + 6, w.w, w.h, 10, 0x000000, 70);
            gfx::fill_round(w.x, w.y, w.w, TITLEH + 12, 10, active ? 0x2D4A9A : 0x353A50);
            draw_text(app_title[a], w.x + 14, w.y + 11, 0xFFFFFF);
            app_draw(a, w.x, w.y + TITLEH, w.w, w.h - TITLEH);
        }
    }
    // Таскбар
    gfx::fill_rect_a(0, SCRH - TASKBAR_H, SCRW, TASKBAR_H, 0x10142A, 215);
    gfx::fill_round(6, SCRH - TASKBAR_H + 6, 96, 32, 8, C_ACC); draw_text_c("iMimi", 6, SCRH - TASKBAR_H + 6, 96, 32, 0xFFFFFF);
    
    // Курсор
    gfx::fill_rect(mouse::x, mouse::y, 3, 3, 0xFFFFFF);
}

static void handle_press(int px, int py) {
    if (py >= SCRH - TASKBAR_H) return;
    int a = win_at(px, py);
    if (a >= 0) { front(a); Win& w = wins[a]; if (py < w.y + TITLEH) { drag_win = a; drag_dx = px - w.x; drag_dy = py - w.y; return; } app_click(a, px, py, w.x, w.y + TITLEH, w.w, w.h - TITLEH); return; }
    for (int i = 0; i < 6; i++) if (hit(desk_icon(i), px, py)) { wins[desk_apps[i]].open = true; wins[desk_apps[i]].min = false; front(desk_apps[i]); return; }
}
static void handle_release() { drag_win = -1; }
static void on_key(int k) {
    if (k == 27) { int t = top_win(); if (t >= 0) wins[t].open = false; }
    int t = top_win();
    if (t == APP_BROWSER) {
        if (k == '\n') browser_load(); // Enter — загрузить страницу
    }
    if (k == K_UP) web_scroll--;
    if (k == K_DOWN) web_scroll++;
}

static void splash(int stage) {
    gfx::fill_rect(0, 0, SCRW, SCRH, 0xFFFFFF);
    int dh = 240, dw = img_base_w * dh / img_base_h;
    gfx::draw_image(SCRW/2 - dw/2, SCRH/2 - 190, dw, dh, img_base_rle, img_base_size, img_base_w, img_base_h);
    draw_text_c("iMimi OS", 0, SCRH/2 + 70, SCRW, 24, 0x1F2937, 3);
    gfx::fill_round(SCRW/2 - 150, SCRH/2 + 140, 300, 8, 4, 0xE5E7EB);
    gfx::fill_round(SCRW/2 - 150, SCRH/2 + 140, 300 * (stage+1) / 3, 8, 4, C_ACC);
    gfx::swap();
}

extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic; gfx::init(mb); mouse::init(); load_settings(); fs_init(); init_windows(); net_init();
    splash(0); for(volatile int i=0;i<50000000;i++); splash(1); for(volatile int i=0;i<50000000;i++); splash(2);
    render(); gfx::swap();
    while (1) {
        bool need = false;
        while (inb(0x64) & 1) { u8 b = inb(0x60); if (b & 0x20) mouse::feed(b); else kbd::feed(b); need = true; }
        if (mouse::ev_press) { mouse::ev_press = false; handle_press(mouse::px, mouse::py); need = true; }
        if (mouse::ev_release) { mouse::ev_release = false; handle_release(); need = true; }
        if (clicker_squish > 0) { clicker_squish--; need = true; }
        if (need) { render(); gfx::swap(); }
    }
}