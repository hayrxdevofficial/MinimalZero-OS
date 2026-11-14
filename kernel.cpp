// ============================================================
//  iMimi OS — Mobile Portrait (Fixed Mouse & Touch)
// ============================================================

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)       { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outw(u16 p, u16 v) { asm volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }

struct multiboot_info {
    u32 flags; u32 mem_lower, mem_upper; u32 boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4]; u32 mmap_length, mmap_addr; u32 drives_length, drives_addr;
    u32 config_table, boot_loader_name, apm_table; u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr; u32 framebuffer_pitch; u32 framebuffer_width; u32 framebuffer_height;
    u8  framebuffer_bpp; u8  framebuffer_type;
} __attribute__((packed));

#include "images.h"

// --- PS/2 МЫШЬ (по мотивам рабочего кода) ---
namespace mouse {
    int x = 240, y = 400;
    u8  btn = 0;
    u8  pkt[3];
    int idx = 0;

    void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t); }
    bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t); return t > 0; }
    void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    u8   read() { return wait_r() ? inb(0x60) : 0; }

    void init() {
        wait_w(); outb(0x64, 0xA8);
        wait_w(); outb(0x64, 0x20);
        u8 status = read();
        status &= ~0x20;   // Включаем тактирование (Clock Enable)
        status &= ~0x02;   // Запрещаем IRQ12 (мы polling)
        wait_w(); outb(0x64, 0x60);
        wait_w(); outb(0x60, status);
        write(0xF6); read(); // Reset
        write(0xF4); read(); // Enable Data Reporting
    }

    bool feed(u8 b) {
        if (idx == 0 && !(b & 0x08)) return false;
        pkt[idx++] = b;
        if (idx < 3) return false;
        idx = 0;
        int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
        int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
        x += dx;
        y -= dy;
        if (x < 0) x = 0; if (x > 479) x = 479;
        if (y < 0) y = 0; if (y > 799) y = 799;
        btn = pkt[0] & 7;
        return true;
    }
}

bool use_hayricon = false;
int coins = 0;
bool unlocked_teen = false;
bool unlocked_cool = false;
bool has_auto_click = false;
int auto_speed_level = 0;
int click_power_level = 0;
int auto_click_timer = 0;
bool power_menu_open = false;
int volume = 50;
int calc_display = 0;
int calc_stored = 0;
int calc_op = 0;
bool calc_new_num = true;

void play_sound(u32 freq) {
    if (volume == 0) return;
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6); outb(0x42, (u8)(div)); outb(0x42, (u8)(div >> 8));
    u8 tmp = inb(0x61); if (tmp != (tmp | 3)) outb(0x61, tmp | 3);
}
void stop_sound() { outb(0x61, inb(0x61) & 0xFC); }

u8 cmos_read(u8 reg) { outb(0x70, reg); return inb(0x71); }
void cmos_write(u8 reg, u8 val) { outb(0x70, (reg | 0x80)); outb(0x71, val); outb(0x70, 0); }
void save_settings() {
    cmos_write(0x40, use_hayricon ? 8 : 0);
    cmos_write(0x41, (u8)(coins >> 24)); cmos_write(0x42, (u8)(coins >> 16)); cmos_write(0x43, (u8)(coins >> 8)); cmos_write(0x44, (u8)coins);
    cmos_write(0x45, (unlocked_teen?1:0) | (unlocked_cool?2:0) | (has_auto_click?4:0));
    cmos_write(0x46, (u8)auto_speed_level); cmos_write(0x47, (u8)click_power_level); cmos_write(0x48, (u8)volume);
}
void load_settings() {
    u8 flags = cmos_read(0x40); if (flags == 0xFF) return;
    use_hayricon = (flags & 8) != 0;
    coins = (cmos_read(0x41) << 24) | (cmos_read(0x42) << 16) | (cmos_read(0x43) << 8) | cmos_read(0x44);
    u8 g = cmos_read(0x45);
    unlocked_teen = (g & 1); unlocked_cool = (g & 2); has_auto_click = (g & 4);
    auto_speed_level = cmos_read(0x46); click_power_level = cmos_read(0x47); volume = cmos_read(0x48);
    if (volume > 100) volume = 50; if (coins < 0) coins = 0;
}
void reboot() { save_settings(); outb(0x64, 0xFE); }
void shutdown() { save_settings(); outw(0x604, 0x2000); while(1) asm volatile("cli; hlt"); }

namespace gfx {
    static u8* fb = nullptr;
    static u32 W = 480, H = 800, pitch = 0;
    static u8 bpp = 32;
    static u32 back_buffer[480 * 800];

    void init(multiboot_info* mb) {
        if (mb->flags & (1u << 12)) {
            fb = (u8*)(u32)mb->framebuffer_addr; W = mb->framebuffer_width; H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch; bpp = mb->framebuffer_bpp;
        }
    }
    inline void put_pixel(u32 x, u32 y, u32 color) { if (x >= W || y >= H) return; back_buffer[y * W + x] = color; }
    void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) { for (u32 j = 0; j < h; j++) for (u32 i = 0; i < w; i++) put_pixel(x + i, y + j, color); }
    void fill_screen(u32 color) { fill_rect(0, 0, W, H, color); }
    inline u32 rgb565_to_888(u16 c) {
        u8 r = ((c >> 11) & 0x1F) * 255 / 31; u8 g = ((c >> 5) & 0x3F) * 255 / 63; u8 b = (c & 0x1F) * 255 / 31;
        return (r << 16) | (g << 8) | b;
    }
    void draw_image_scaled(int ox, int oy, int draw_w, int draw_h, const u16* rle, u32 rle_size, int img_w, int img_h) {
        u32 idx = 0; int x = 0, y = 0;
        while (idx < rle_size) {
            u16 header = rle[idx++];
            if (header & 0x8000) {
                u32 len = header & 0x7FFF; u32 col = rgb565_to_888(rle[idx++]);
                for (u32 i = 0; i < len; i++) {
                    put_pixel(ox + (x * draw_w) / img_w, oy + (y * draw_h) / img_h, col);
                    if (++x >= img_w) { x = 0; y++; }
                }
            } else {
                u32 len = header;
                for (u32 i = 0; i < len; i++) {
                    u32 col = rgb565_to_888(rle[idx++]);
                    put_pixel(ox + (x * draw_w) / img_w, oy + (y * draw_h) / img_h, col);
                    if (++x >= img_w) { x = 0; y++; }
                }
            }
        }
    }
    void swap() {
        if (!fb) return;
        u32* dest = (u32*)fb; u32* src = back_buffer; u32 p32 = pitch / 4;
        if (p32 == W) { for (u32 i = 0; i < W * H; i++) dest[i] = src[i]; }
        else { for (u32 y = 0; y < H; y++) { for (u32 x = 0; x < W; x++) dest[y*p32+x] = src[y*W+x]; } }
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

void draw_char(char c, int x, int y, u32 color) {
    if (c < 32 || c > 126) c = '?';
    const u8* g = font8x8[c - 32];
    for (int r = 0; r < 8; r++) for (int cl = 0; cl < 8; cl++)
        if (g[r] & (1 << (7 - cl))) gfx::put_pixel(x + cl, y + r, color);
}
void draw_string(const char* s, int x, int y, u32 color) { while (*s) { draw_char(*s++, x, y, color); x += 8; } }
void draw_int(int num, int x, int y, u32 color) {
    char buf[24]; int i = 0;
    if (num == 0) { draw_char('0', x, y, color); return; }
    bool neg = num < 0; if (neg) num = -num;
    while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; }
    if (neg) buf[i++] = '-';
    while (i > 0) { i--; draw_char(buf[i], x, y, color); x += 8; }
}
void draw_cursor(int x, int y) {
    gfx::fill_rect(x-5, y-5, 10, 10, 0xFF0000);
    gfx::fill_rect(x-3, y-3, 6, 6, 0xFFFFFF);
}

extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic; gfx::init(mb); mouse::init(); load_settings();
    
    int state = 0, current_mimi = 0, squish_pixels = 0, clicker_dir = 0;
    u8 prev_btn = 0;
    u32 boot_bg = use_hayricon ? 0x121212 : 0xFFFFFF;

    // Загрузка 3 сек
    gfx::fill_screen(boot_bg);
    if (use_hayricon) gfx::draw_image_scaled(110, 200, 260, 260, img_hayrulla_rle, img_hayrulla_size, img_hayrulla_w, img_hayrulla_h);
    else gfx::draw_image_scaled(110, 200, 260, 260, img_base_rle, img_base_size, img_base_w, img_base_h);
    gfx::swap();
    for(volatile u32 i = 0; i < 180000000; i++);

    state = 1;

    while (1) {
        // 1. Читаем PS/2 пока есть данные
        for (int i = 0; i < 32; i++) {
            u8 st = inb(0x64);
            if (!(st & 1)) break;
            u8 b = inb(0x60);
            if (st & 0x20) { mouse::feed(b); }
            else {
                if (b == 0x3B) power_menu_open = !power_menu_open;
                if (b == 0x3C) { if (volume < 100) volume += 10; save_settings(); }
                if (b == 0x3D) { if (volume > 0) volume -= 10; save_settings(); }
            }
        }

        // 2. Детектор нажатия (только фронт)
        u8 cur_btn = mouse::btn;
        bool click = (cur_btn & 1) && !(prev_btn & 1);
        prev_btn = cur_btn;

        int mx = mouse::x, my = mouse::y;

        if (state == 1) {
            gfx::fill_screen(0x1A1A2E); gfx::fill_rect(0, 0, 480, 40, 0x111122);
            draw_string("12:00", 20, 15, 0xFFFFFF); draw_string("VOL:", 350, 15, 0x888888); draw_int(volume, 400, 15, 0xFFFFFF);
            draw_string("v2", 460, 15, 0x666666);

            if (use_hayricon) gfx::draw_image_scaled(30, 80, 100, 100, img_hayrulla_rle, img_hayrulla_size, img_hayrulla_w, img_hayrulla_h);
            else gfx::draw_image_scaled(30, 80, 100, 100, img_base_rle, img_base_size, img_base_w, img_base_h);
            draw_string("Clicker", 40, 190, 0xFFFFFF);

            gfx::fill_rect(190, 80, 100, 100, 0x4CAF50); draw_string("Files", 210, 120, 0xFFFFFF); draw_string("Files", 215, 190, 0xFFFFFF);
            gfx::fill_rect(350, 80, 100, 100, 0x9E9E9E); draw_string("Calc", 375, 120, 0x000000); draw_string("Calc", 375, 190, 0xFFFFFF);
            gfx::fill_rect(30, 240, 100, 100, 0x555555); draw_string("Set", 60, 280, 0xFFFFFF); draw_string("Settings", 40, 350, 0xFFFFFF);

            if (click) {
                if (mx >= 30 && mx <= 130 && my >= 80 && my <= 180) { state = 2; play_sound(1000); for(volatile int i=0; i<2000000; i++); stop_sound(); }
                if (mx >= 190 && mx <= 290 && my >= 80 && my <= 180) { state = 3; play_sound(1000); for(volatile int i=0; i<2000000; i++); stop_sound(); }
                if (mx >= 350 && mx <= 450 && my >= 80 && my <= 180) { state = 5; calc_display = 0; calc_new_num = true; play_sound(1000); for(volatile int i=0; i<2000000; i++); stop_sound(); }
                if (mx >= 30 && mx <= 130 && my >= 240 && my <= 340) { state = 4; play_sound(1000); for(volatile int i=0; i<2000000; i++); stop_sound(); }
            }
        } else if (state == 2) {
            gfx::fill_screen(0xFFFFFF); gfx::fill_rect(10, 10, 80, 40, 0xEEEEEE); draw_string("< Back", 20, 25, 0x000000);
            draw_string("Coins: ", 300, 25, 0xFFAA00); draw_int(coins, 370, 25, 0xFFAA00);
            if (has_auto_click) { auto_click_timer++; int tt = 100 - auto_speed_level*30; if(tt<10)tt=10; if(auto_click_timer>=tt){auto_click_timer=0; coins += (click_power_level>0)?2:1;} }
            gfx::fill_rect(10, 600, 140, 60, unlocked_teen ? 0xCCCCCC : 0xEEEEEE); draw_string(unlocked_teen ? "Teen" : "Buy Teen", 20, 620, 0x000000);
            gfx::fill_rect(170, 600, 140, 60, unlocked_cool ? 0xCCCCCC : 0xEEEEEE); draw_string(unlocked_cool ? "Cool" : "Buy Cool", 180, 620, 0x000000);
            gfx::fill_rect(330, 600, 140, 60, 0xEEEEEE); draw_string("Mini", 380, 620, 0x000000);
            gfx::fill_rect(10, 680, 140, 60, has_auto_click ? 0xCCCCCC : 0xEEEEEE); draw_string(has_auto_click ? "Auto" : "Buy Auto", 20, 700, 0x000000);
            int sc = 20 + auto_speed_level*10; gfx::fill_rect(170, 680, 140, 60, 0xEEEEEE); draw_string("Speed+1", 190, 700, 0x000000);
            gfx::fill_rect(330, 680, 140, 60, click_power_level > 0 ? 0xCCCCCC : 0xEEEEEE); draw_string(click_power_level>0?"Power Max":"Power+1", 340, 700, 0x000000);
            if (click) {
                if (mx >= 10 && mx <= 90 && my >= 10 && my <= 50) state = 1;
                if (mx >= 10 && mx <= 150 && my >= 600 && my <= 660) { if (unlocked_teen) current_mimi = 1; else if (coins >= 10) { coins -= 10; unlocked_teen = true; current_mimi = 1; save_settings(); } }
                if (mx >= 170 && mx <= 310 && my >= 600 && my <= 660) { if (unlocked_cool) current_mimi = 2; else if (coins >= 25) { coins -= 25; unlocked_cool = true; current_mimi = 2; save_settings(); } }
                if (mx >= 330 && mx <= 470 && my >= 600 && my <= 660) current_mimi = 0;
                if (mx >= 10 && mx <= 150 && my >= 680 && my <= 740) { if (!has_auto_click && coins >= 15) { coins -= 15; has_auto_click = true; save_settings(); } }
                if (mx >= 170 && mx <= 310 && my >= 680 && my <= 740) { if (coins >= sc) { coins -= sc; auto_speed_level++; save_settings(); } }
                if (mx >= 330 && mx <= 470 && my >= 680 && my <= 740) { if (click_power_level == 0 && coins >= 30) { coins -= 30; click_power_level = 1; save_settings(); } }
            }
            int ox = 0, oy = 0, dw = 0, dh = 0;
            if (current_mimi == 0) { dh = 350-squish_pixels; dw = img_base_w*dh/img_base_h; ox=(480-dw)/2; oy=200; gfx::draw_image_scaled(ox,oy,dw,dh,img_base_rle,img_base_size,img_base_w,img_base_h); }
            else if (current_mimi == 1) { dh = 350-squish_pixels; dw = img_adult_w*dh/img_adult_h; ox=(480-dw)/2; oy=200; gfx::draw_image_scaled(ox,oy,dw,dh,img_adult_rle,img_adult_size,img_adult_w,img_adult_h); }
            else { dh = 350-squish_pixels; dw = img_cool_w*dh/img_cool_h; ox=(480-dw)/2; oy=200; gfx::draw_image_scaled(ox,oy,dw,dh,img_cool_rle,img_cool_size,img_cool_w,img_cool_h); }
            if (click && mx >= ox && mx <= ox+dw && my >= oy && my <= oy+dh) { coins += (click_power_level>0)?2:1; squish_pixels=4; clicker_dir=1; play_sound(1200); for(volatile int i=0;i<1000000;i++); stop_sound(); }
            if (clicker_dir==1) { for(volatile int i=0;i<1000000;i++); squish_pixels--; if(squish_pixels<=0){squish_pixels=0;clicker_dir=0;} }
        } else if (state == 3) {
            gfx::fill_screen(0x1A1A2E); int wx=20,wy=100,ww=440,wh=600;
            gfx::fill_rect(wx,wy,ww,wh,0xFFFFFF); gfx::fill_rect(wx,wy,ww,50,0x3498DB);
            draw_string("Files - 5 GB", wx+20, wy+15, 0xFFFFFF); gfx::fill_rect(wx+ww-50, wy+10, 40, 30, 0xE74C3C); draw_string("X", wx+ww-35, wy+20, 0xFFFFFF);
            draw_string("Total: 5.0 GB", wx+30, wy+80, 0x000000); draw_string("Used:  0.1 GB", wx+30, wy+120, 0x000000); draw_string("Free:  4.9 GB", wx+30, wy+160, 0x000000);
            if (click && mx >= wx+ww-50 && mx <= wx+ww-10 && my >= wy+10 && my <= wy+40) state = 1;
        } else if (state == 4) {
            gfx::fill_screen(0x1A1A2E); int wx=20,wy=150,ww=440,wh=300;
            gfx::fill_rect(wx,wy,ww,wh,0xFFFFFF); gfx::fill_rect(wx,wy,ww,50,0x333333);
            draw_string("Settings", wx+20, wy+15, 0xFFFFFF); gfx::fill_rect(wx+ww-50, wy+10, 40, 30, 0xE74C3C); draw_string("X", wx+ww-35, wy+20, 0xFFFFFF);
            gfx::fill_rect(wx+20, wy+80, 400, 60, use_hayricon ? 0x4CAF50 : 0xEEEEEE);
            draw_string("HayrICons", wx+30, wy+105, use_hayricon?0xFFFFFF:0x000000); draw_string(use_hayricon?"ON":"OFF", wx+350, wy+105, use_hayricon?0xFFFFFF:0x000000);
            if (click) {
                if (mx >= wx+ww-50 && mx <= wx+ww-10 && my >= wy+10 && my <= wy+40) state = 1;
                if (mx >= wx+20 && mx <= wx+420 && my >= wy+80 && my <= wy+140) { use_hayricon = !use_hayricon; save_settings(); }
            }
        } else if (state == 5) {
            gfx::fill_screen(0x1A1A2E); int wx=20,wy=100,ww=440,wh=600;
            gfx::fill_rect(wx,wy,ww,wh,0x333333); gfx::fill_rect(wx+10, wy+10, 80, 50, 0xE74C3C); draw_string("< Back", wx+20, wy+25, 0xFFFFFF);
            gfx::fill_rect(wx+20, wy+80, ww-40, 80, 0xCCCCCC); draw_int(calc_display, wx+30, wy+110, 0x000000);
            const char* L[16] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
            int bw=90,bh=80,gap=10; int sx=wx+20, sy=wy+180;
            for (int r=0;r<4;r++) for (int c=0;c<4;c++) {
                int bx=sx+c*(bw+gap), by=sy+r*(bh+gap);
                bool hov = (mx>=bx && mx<=bx+bw && my>=by && my<=by+bh);
                gfx::fill_rect(bx,by,bw,bh, hov?0xAAAAAA:0xEEEEEE); draw_string(L[r*4+c], bx+35, by+30, 0x000000);
                if (click && hov) {
                    int bi = r*4+c; play_sound(1500); for(volatile int i=0;i<1000000;i++); stop_sound();
                    if (bi==12){calc_display=0;calc_new_num=true;}
                    else if (bi==14){ if(calc_op==1)calc_display=calc_stored+calc_display; if(calc_op==2)calc_display=calc_stored-calc_display; if(calc_op==3)calc_display=calc_stored*calc_display; if(calc_op==4){if(calc_display!=0)calc_display=calc_stored/calc_display;} calc_op=0;calc_new_num=true; }
                    else if (bi==3||bi==7||bi==11||bi==15){ calc_stored=calc_display; calc_op=(bi==3)?4:(bi==7)?3:(bi==11)?2:1; calc_new_num=true; }
                    else { int num = bi==0?7:bi==1?8:bi==2?9:bi==4?4:bi==5?5:bi==6?6:bi==8?1:bi==9?2:bi==10?3:bi==13?0:0; if(calc_new_num){calc_display=num;calc_new_num=false;}else{if(calc_display<99999999)calc_display=calc_display*10+num;} }
                }
            }
            if (click && mx >= wx+10 && mx <= wx+90 && my >= wy+10 && my <= wy+60) state = 1;
        }

        if (power_menu_open) {
            gfx::fill_rect(40,250,400,200,0x222222); gfx::fill_rect(40,250,400,40,0x333333); draw_string("Power Menu", 60, 260, 0xFFFFFF);
            gfx::fill_rect(390,255,40,30,0xE74C3C); draw_string("X", 405, 265, 0xFFFFFF);
            gfx::fill_rect(70,320,340,50,0xE74C3C); draw_string("Restart", 190, 335, 0xFFFFFF);
            gfx::fill_rect(70,380,340,50,0xE74C3C); draw_string("Shutdown", 180, 395, 0xFFFFFF);
            if (click) {
                if (mx>=390 && mx<=430 && my>=255 && my<=285) power_menu_open = false;
                if (mx>=70 && mx<=410 && my>=320 && my<=370) reboot();
                if (mx>=70 && mx<=410 && my>=380 && my<=430) shutdown();
            }
        }

        draw_cursor(mx, my);
        gfx::swap();
    }
}
