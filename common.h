#pragma once

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed int          s32;
typedef signed char         s8;
typedef signed short        s16;

static inline void outb(u16 p, u8 v)  { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)         { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline u16  inw(u16 p)         { u16 v; asm volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }

extern "C" {
void* memset(void* d, int c, unsigned int n) {
    u8* p = (u8*)d; 
    while (n--) { *p++ = (u8)c; } 
    return d;
}
void* memcpy(void* d, const void* s, unsigned int n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    while (n--) { *a++ = *b++; } 
    return d;
}
void* memmove(void* d, const void* s, unsigned int n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    if (a < b) { while (n--) { *a++ = *b++; } }
    else { a += n; b += n; while (n--) { *--a = *--b; } }
    return d;
}
int memcmp(const void* a, const void* b, unsigned int n) {
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
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

// ====== Строковые утилиты ======
static inline int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static inline void scopy(char* d, const char* s, int max) {
    int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0;
}
static inline bool seq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; } return *a == *b;
}
static inline int sappend(char* d, int pos, const char* s, int max) {
    while (*s && pos < max - 1) { d[pos++] = *s++; }
    d[pos] = 0; 
    return pos;
}
static inline int itoa_(int v, char* buf) {
    char t[16]; int n = 0, i = 0; bool neg = v < 0;
    unsigned int u = neg ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = '0' + (u % 10); u /= 10; }
    if (neg) buf[i++] = '-';
    while (n) buf[i++] = t[--n];
    buf[i] = 0; return i;
}

// ====== Глобальные переменные ======
extern int  g_scr_w, g_scr_h;
extern int  g_mx, g_my;
extern u8   g_mbtn;
extern bool g_press, g_release;
extern int  g_px, g_py;
extern int  g_mspeed;
extern u32  g_ticks;
extern int  g_volume;
extern bool g_kb_mouse, g_hayricon;
extern int  g_coins;
extern bool g_teen_unlocked, g_cool_unlocked, g_auto_click;
extern int  g_auto_level, g_click_power;
extern int  g_cur_skin;

// ====== Время ======
u32 time_ms();
void time_init();
void rtc_get(int& h, int& m, int& s);

// ====== Звук ======
void beep(u32 freq, u32 ms);

// ====== Сохранения ======
void settings_save();
void settings_load();

// ====== Клавиатура ======
enum { K_UP=0x101, K_DOWN, K_LEFT, K_RIGHT, K_DEL, K_HOME, K_END,
       K_F1=0x110, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8,
       K_F9, K_F10, K_F11, K_F12, K_TAB, K_PGUP, K_PGDN };
void on_key(int k);

// ====== Сеть ======
void net_init();
void net_open_url(const char* url);
void net_close_browser();
void net_download(const char* url, const char* saveas);
void net_run_js(const char* app_name);
int  net_poll_byte();
int  net_receive_file(u8* out, int max_sz);

// ====== Файловая система ======
#define MAXN    128
#define NAMELEN 32
#define DATALEN 1024

struct Node {
    bool used, dir, trashed;
    int parent;
    char name[NAMELEN];
    int len;
    char data[DATALEN];
};

extern Node fs[MAXN];
extern int fs_docs, fs_pics;

int  fs_new(int par, const char* nm, bool dir);
void fs_set_txt(int idx, const char* t);
int  fs_list(int par, int* out, int max);
void fs_destroy(int idx);
void fs_init();
bool fs_reachable(int idx);
void fs_path(int idx, char* out, int max);

// ====== GFX namespace ======
namespace gfx {
    void init(multiboot_info* mb);
    bool is_ok();
    u32  width();
    u32  height();
    void put(int x, int y, u32 c);
    u32  get(int x, int y);
    void rect(int x, int y, int w, int h, u32 c);
    void rect_a(int x, int y, int w, int h, u32 c, u32 a);
    void grad_v(int x, int y, int w, int h, u32 c1, u32 c2);
    void grad_h(int x, int y, int w, int h, u32 c1, u32 c2);
    void round(int x, int y, int w, int h, int r, u32 c);
    void round_a(int x, int y, int w, int h, int r, u32 c, u32 a);
    void outline(int x, int y, int w, int h, u32 c);
    void outline_round(int x, int y, int w, int h, int r, u32 c);
    void circle(int cx, int cy, int r, u32 c);
    void circle_a(int cx, int cy, int r, u32 c, u32 a);
    void ring(int cx, int cy, int r_out, int r_in, u32 c);
    void image(int ox, int oy, int dw, int dh, const u16* rle, u32 size, int iw, int ih);
    void clear(u32 c);
    void swap();
    u32  lerp(u32 c1, u32 c2, int t, int tmax);
    u32  rgb565(u16 c);
}

// ====== TEXT namespace ======
namespace text {
    void draw_char(char c, int x, int y, u32 color, int scale = 1);
    void draw(const char* s, int x, int y, u32 color, int scale = 1);
    void draw_n(const char* s, int x, int y, u32 color, int max_chars);
    int  width(const char* s, int scale = 1);
    void draw_c(const char* s, int x, int y, int w, int h, u32 color, int scale = 1);
    void draw_int(int v, int x, int y, u32 color, int scale = 1);
    void draw_int_c(int v, int x, int y, int w, int h, u32 color, int scale = 1);
    void draw_truncated(const char* s, int x, int y, int w, u32 color);
}

// ====== MOUSE namespace ======
namespace mouse {
    void init();
    bool feed(u8 b);
}

// ====== KBD namespace ======
namespace kbd {
    void feed(u8 b);
}