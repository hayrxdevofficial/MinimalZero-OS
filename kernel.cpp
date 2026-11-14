// ============================================================
//  iMimi OS — Safe Interrupts & Settings
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

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
int mouse_x = 400, mouse_y = 300;
bool mouse_left_clicked = false;

bool use_touch = false;
bool use_mouse = true;
bool use_kb_mouse = true;

int coins = 0;
bool unlocked_adult = false;
bool unlocked_cool = false;
bool has_auto_click = false;
int auto_speed_level = 0;
int click_power_level = 0;
int auto_click_timer = 0;

bool power_menu_open = false;
volatile u32 system_ticks = 0;

u8 mouse_cycle = 0;
u8 mouse_byte[3];

// --- СИСТЕМНЫЕ ПРЕРЫВАНИЯ ---
extern "C" void isr0();
extern "C" void isr1();
extern "C" void isr12();

extern "C" void timer_handler_c() {
    system_ticks++;
    outb(0x20, 0x20);
}

// Заглушки: просто отправляем EOI (End of Interrupt) контроллеру
asm(
".global isr1\n isr1:\n pusha\n movb $0x20, %al\n outb %al, $0x20\n popa\n iret\n"
".global isr12\n isr12:\n pusha\n movb $0x20, %al\n outb %al, $0xA0\n outb %al, $0x20\n popa\n iret\n"
);
asm(".global isr0\n isr0:\n pusha\n call timer_handler_c\n popa\n iret\n");

void init_timer() {
    // Настраиваем PIT
    u32 divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));

    // Разрешаем прерывания клавиатуры (IRQ1) и мыши (IRQ12) в PIC
    outb(0x21, 0xF6);
    outb(0xA1, 0xEF);

    struct IDT_entry { u16 base_low; u16 sel; u8 zero; u8 flags; u16 base_high; } __attribute__((packed));
    struct IDT_ptr { u16 limit; u32 base; } __attribute__((packed));
    static IDT_entry idt[256];
    static IDT_ptr idtp;

    idtp.limit = (sizeof(IDT_entry) * 256) - 1; idtp.base = (u32)&idt;
    for(int i = 0; i < 256; i++) { idt[i].base_low = 0; idt[i].sel = 0; idt[i].zero = 0; idt[i].flags = 0; idt[i].base_high = 0; }

    u32 base0 = (u32)isr0;
    idt[0x20].base_low = base0 & 0xFFFF; idt[0x20].sel = 0x08; idt[0x20].flags = 0x8E; idt[0x20].base_high = (base0 >> 16) & 0xFFFF;

    u32 base1 = (u32)isr1;
    idt[0x21].base_low = base1 & 0xFFFF; idt[0x21].sel = 0x08; idt[0x21].flags = 0x8E; idt[0x21].base_high = (base1 >> 16) & 0xFFFF;

    u32 base12 = (u32)isr12;
    idt[0x2C].base_low = base12 & 0xFFFF; idt[0x2C].sel = 0x08; idt[0x2C].flags = 0x8E; idt[0x2C].base_high = (base12 >> 16) & 0xFFFF;

    asm volatile("lidt (%0)" :: "r"(&idtp));
    asm volatile("sti");
}

void reboot() { outb(0x64, 0xFE); }
void shutdown() { outw(0x604, 0x2000); while(1) asm volatile("cli; hlt"); }

// --- ОПРОС ВВОДА (Ограниченный цикл) ---
void poll_inputs() {
    for (int i = 0; i < 16; i++) { // Читаем максимум 16 байт за кадр
        if (!(inb(0x64) & 1)) break;
        
        u8 status = inb(0x64);
        u8 data = inb(0x60);

        if (status & 0x20) {
            if (!use_mouse && !use_touch) continue;
            mouse_byte[mouse_cycle++] = data;
            if (mouse_cycle == 3) {
                mouse_cycle = 0;
                if (mouse_byte[0] & 0x01) mouse_left_clicked = true;
                if (!(mouse_byte[0] & 0x08)) {
                    int dx = (mouse_byte[0] & 0x10) ? (mouse_byte[1] - 256) : mouse_byte[1];
                    int dy = (mouse_byte[0] & 0x20) ? (mouse_byte[2] - 256) : mouse_byte[2];
                    mouse_x += dx;
                    mouse_y -= dy;
                }
            }
        } else {
            if (!use_kb_mouse) {
                if (data == 0x1C || data == 0x39) mouse_left_clicked = true;
                continue;
            }
            if (data == 0x48 || data == 0x11) mouse_y -= 20;
            if (data == 0x50 || data == 0x1F) mouse_y += 20;
            if (data == 0x4B || data == 0x1E) mouse_x -= 20;
            if (data == 0x4D || data == 0x20) mouse_x += 20;
            if (data == 0x1C || data == 0x39) mouse_left_clicked = true;
            if (data == 0x01) { mouse_x = 10; mouse_y = 10; mouse_left_clicked = true; }
        }
    }
}

// --- ГРАФИКА ---
namespace gfx {
    static u8* fb = nullptr; 
    static u32 W = 800, H = 600, pitch = 0; 
    static u8 bpp = 32;
    static u32 back_buffer[800 * 600]; 

    void init(multiboot_info* mb) {
        if (mb->flags & (1u << 12)) {
            fb = (u8*)(u32)mb->framebuffer_addr; 
            W = mb->framebuffer_width; H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch; bpp = mb->framebuffer_bpp;
        }
    }

    inline void put_pixel(u32 x, u32 y, u32 color) {
        if (x >= W || y >= H) return;
        back_buffer[y * W + x] = color;
    }

    void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
        for (u32 j = 0; j < h; j++) for (u32 i = 0; i < w; i++) put_pixel(x + i, y + j, color);
    }
    void fill_screen(u32 color) { fill_rect(0, 0, W, H, color); }

    inline u32 rgb565_to_888(u16 c) {
        u8 r = ((c >> 11) & 0x1F) * 255 / 31; u8 g = ((c >> 5) & 0x3F) * 255 / 63; u8 b = (c & 0x1F) * 255 / 31;
        return (r << 16) | (g << 8) | b;
    }

    void draw_image_scaled(int ox, int oy, int draw_w, int draw_h, const u16* rle, u32 rle_size, int img_w, int img_h) {
        u32 idx = 0; int x = 0, y = 0;
        while (idx < rle_size) {
            u16 header = rle[idx++];
            u32 len = (header & 0x8000) ? (header & 0x7FFF) : header;
            for (u32 i = 0; i < len; i++) {
                u32 col = rgb565_to_888(rle[idx++]);
                put_pixel(ox + (x * draw_w) / img_w, oy + (y * draw_h) / img_h, col);
                if (++x >= img_w) { x = 0; y++; }
            }
        }
    }

    void draw_image_centered(int target_h, const u16* rle, u32 rle_size, int img_w, int img_h, int squish_pixels) {
        int draw_h = target_h - squish_pixels;
        int draw_w = (img_w * draw_h) / img_h; 
        int ox = (W - draw_w) / 2;
        int oy = (H - draw_h) / 2 - 30;
        draw_image_scaled(ox, oy, draw_w, draw_h, rle, rle_size, img_w, img_h);
    }

    void swap() {
        if (!fb) return;
        u32* dest = (u32*)fb;
        u32* src = back_buffer;
        u32 p32 = pitch / 4;
        if (p32 == W) {
            for (u32 i = 0; i < W * H; i++) dest[i] = src[i];
        } else {
            for (u32 y = 0; y < H; y++) {
                u32* row_dest = dest + (y * p32);
                u32* row_src = src + (y * W);
                for (u32 x = 0; x < W; x++) row_dest[x] = row_src[x];
            }
        }
    }
}

// --- ШРИФТ И ТЕКСТ ---
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
void draw_string(const char* s, int x, int y, u32 color) {
    while (*s) { draw_char(*s++, x, y, color); x += 8; }
}
void draw_int(int num, int x, int y, u32 color) {
    char buf[12]; int i = 0;
    if (num == 0) buf[i++] = '0';
    while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; }
    while (i > 0) { i--; draw_char(buf[i], x, y, color); x += 8; }
}

void draw_cursor(int x, int y) {
    static const u8 cursor[16][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0,0},{1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},{1,2,2,2,1,0,0,0,0,0,0,0},{1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},{1,2,2,2,2,2,2,1,0,0,0,0},{1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0},{1,2,2,2,2,1,1,1,1,1,1,0},{1,2,1,2,2,1,0,0,0,0,0,0},
        {1,1,0,1,2,2,1,0,0,0,0,0},{1,0,0,0,1,2,2,1,0,0,0,0},{0,0,0,0,1,2,2,1,0,0,0,0},
        {0,0,0,0,0,1,1,0,0,0,0,0}
    };
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 12; i++) {
            if (cursor[j][i] == 1) gfx::put_pixel(x + i, y + j, 0x000000);
            if (cursor[j][i] == 2) gfx::put_pixel(x + i, y + j, 0xFFFFFF);
        }
    }
}

// ============================================================
//  ОСНОВНАЯ ЛОГИКА
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic; 
    gfx::init(mb); 
    init_timer();
    
    int state = 0; 
    int current_mimi = 0; 
    int squish_pixels = 0; 
    int clicker_dir = 0;     

    while (system_ticks < 600) {
        gfx::fill_screen(0xFFFFFF); 
        gfx::draw_image_centered(300, img_base_rle, img_base_size, img_base_w, img_base_h, 0);
        int progress = (system_ticks * 400) / 600;
        gfx::fill_rect(200, 500, 400, 20, 0xDDDDDD);
        gfx::fill_rect(200, 500, progress, 20, 0x3498DB);
        gfx::swap();
    }

    state = 1; 

    while (1) {
        poll_inputs(); 
        bool click = mouse_left_clicked; 
        mouse_left_clicked = false;

        if (mouse_x < 0) { mouse_x = 0; }
        if (mouse_x > 799) { mouse_x = 799; }
        if (mouse_y < 0) { mouse_y = 0; }
        if (mouse_y > 599) { mouse_y = 599; }

        if (state == 1) {
            gfx::fill_screen(0x1A1A2E); 
            gfx::fill_rect(0, 0, 800, 30, 0x111122);
            
            draw_string("12:00", 20, 10, 0xFFFFFF); 
            draw_string("RAM: 8 MB / 1024 MB", 200, 10, 0x888888);
            
            gfx::fill_rect(750, 0, 50, 30, 0x333333);
            draw_string("PWR", 760, 10, 0xFFFFFF);

            gfx::draw_image_scaled(50, 70, 80, 80, img_base_rle, img_base_size, img_base_w, img_base_h);
            draw_string("Mimi Clicker", 40, 160, 0xFFFFFF);

            gfx::fill_rect(160, 70, 80, 80, 0x4CAF50);
            draw_string("Files", 180, 110, 0xFFFFFF);
            draw_string("Files", 180, 160, 0xFFFFFF);

            if (click) {
                if (mouse_x >= 50 && mouse_x <= 130 && mouse_y >= 70 && mouse_y <= 150) { state = 2; }
                if (mouse_x >= 160 && mouse_x <= 240 && mouse_y >= 70 && mouse_y <= 150) { state = 3; }
                if (mouse_x >= 750 && mouse_y <= 30) { power_menu_open = !power_menu_open; }
            }

        } else if (state == 2) {
            gfx::fill_screen(0xFFFFFF); 
            gfx::fill_rect(10, 10, 60, 30, 0xEEEEEE);
            draw_string("< Back", 15, 20, 0x000000);
            
            draw_string("Mimi Clicker", 350, 20, 0x000000);
            draw_string("Coins: ", 600, 20, 0xFFAA00);
            draw_int(coins, 660, 20, 0xFFAA00);

            if (has_auto_click) {
                auto_click_timer++;
                int target_time = 100 - (auto_speed_level * 30); 
                if (target_time < 10) target_time = 10;
                if (auto_click_timer >= target_time) {
                    auto_click_timer = 0;
                    coins += (click_power_level > 0) ? 2 : 1; 
                }
            }

            gfx::fill_rect(50, 460, 150, 40, unlocked_adult ? 0xCCCCCC : 0xEEEEEE);
            draw_string(unlocked_adult ? "Adult (Owned)" : "Buy Adult", 60, 475, 0x000000);
            gfx::fill_rect(250, 460, 150, 40, unlocked_cool ? 0xCCCCCC : 0xEEEEEE);
            draw_string(unlocked_cool ? "Cool (Owned)" : "Buy Cool", 270, 475, 0x000000);
            gfx::fill_rect(450, 460, 150, 40, 0xEEEEEE);
            draw_string("Use Mini", 480, 475, 0x000000);

            gfx::fill_rect(50, 520, 150, 40, has_auto_click ? 0xCCCCCC : 0xEEEEEE);
            draw_string(has_auto_click ? "Auto (Owned)" : "Buy Auto", 70, 535, 0x000000);
            int speed_cost = 20 + (auto_speed_level * 10);
            gfx::fill_rect(250, 520, 150, 40, 0xEEEEEE);
            draw_string("Speed +1", 280, 535, 0x000000);
            draw_int(speed_cost, 370, 535, 0x888888);
            gfx::fill_rect(450, 520, 150, 40, click_power_level > 0 ? 0xCCCCCC : 0xEEEEEE);
            draw_string(click_power_level > 0 ? "Power (Max)" : "Power +1", 480, 535, 0x000000);

            if (click) {
                if (mouse_x >= 10 && mouse_x <= 70 && mouse_y >= 10 && mouse_y <= 40) { state = 1; }
                if (mouse_x >= 50 && mouse_x <= 200 && mouse_y >= 460 && mouse_y <= 500) { if (unlocked_adult) current_mimi = 1; else if (coins >= 10) { coins -= 10; unlocked_adult = true; current_mimi = 1; } }
                if (mouse_x >= 250 && mouse_x <= 400 && mouse_y >= 460 && mouse_y <= 500) { if (unlocked_cool) current_mimi = 2; else if (coins >= 25) { coins -= 25; unlocked_cool = true; current_mimi = 2; } }
                if (mouse_x >= 450 && mouse_x <= 600 && mouse_y >= 460 && mouse_y <= 500) { current_mimi = 0; }
                if (mouse_x >= 50 && mouse_x <= 200 && mouse_y >= 520 && mouse_y <= 560) { if (!has_auto_click && coins >= 15) { coins -= 15; has_auto_click = true; } }
                if (mouse_x >= 250 && mouse_x <= 400 && mouse_y >= 520 && mouse_y <= 560) { if (coins >= speed_cost) { coins -= speed_cost; auto_speed_level++; } }
                if (mouse_x >= 450 && mouse_x <= 600 && mouse_y >= 520 && mouse_y <= 560) { if (click_power_level == 0 && coins >= 30) { coins -= 30; click_power_level = 1; } }
            }

            int ox = 0, oy = 0, draw_w = 0, draw_h = 0;
            if (current_mimi == 0) { draw_h = 220 - squish_pixels; draw_w = img_base_w * draw_h / img_base_h; gfx::draw_image_centered(220, img_base_rle, img_base_size, img_base_w, img_base_h, squish_pixels); }
            else if (current_mimi == 1) { draw_h = 220 - squish_pixels; draw_w = img_adult_w * draw_h / img_adult_h; gfx::draw_image_centered(220, img_adult_rle, img_adult_size, img_adult_w, img_adult_h, squish_pixels); }
            else { draw_h = 220 - squish_pixels; draw_w = img_cool_w * draw_h / img_cool_h; gfx::draw_image_centered(220, img_cool_rle, img_cool_size, img_cool_w, img_cool_h, squish_pixels); }

            ox = (800 - draw_w) / 2; oy = (600 - draw_h) / 2 - 50;
            if (click && mouse_x >= ox && mouse_x <= ox + draw_w && mouse_y >= oy && mouse_y <= oy + draw_h) {
                coins += (click_power_level > 0) ? 2 : 1; 
                squish_pixels = 4; 
                clicker_dir = 1;    
            }
            if (clicker_dir == 1) {
                for(volatile int i=0; i<1000000; i++); 
                squish_pixels--;
                if (squish_pixels <= 0) { squish_pixels = 0; clicker_dir = 0; }
            }
        } else if (state == 3) {
            gfx::fill_screen(0x1A1A2E);
            int wx = 150, wy = 100, ww = 500, wh = 350;
            gfx::fill_rect(wx, wy, ww, wh, 0xFFFFFF);
            gfx::fill_rect(wx, wy, ww, 30, 0x3498DB);
            draw_string("Files - 5 GB Drive", wx + 10, wy + 10, 0xFFFFFF);
            gfx::fill_rect(wx + ww - 30, wy + 5, 20, 20, 0xE74C3C);
            draw_string("X", wx + ww - 25, wy + 10, 0xFFFFFF);

            draw_string("Total Space: 5.0 GB", wx + 30, wy + 60, 0x000000);
            draw_string("Used Space:  0.1 GB", wx + 30, wy + 90, 0x000000);
            draw_string("Free Space:  4.9 GB", wx + 30, wy + 120, 0x000000);
            
            gfx::fill_rect(wx + 30, wy + 160, 440, 20, 0xEEEEEE);
            gfx::fill_rect(wx + 30, wy + 160, 10, 20, 0x4CAF50);

            draw_string("Documents", wx + 30, wy + 220, 0x000000);
            draw_string("Pictures", wx + 30, wy + 250, 0x000000);

            if (click) {
                if (mouse_x >= wx + ww - 30 && mouse_x <= wx + ww - 10 && mouse_y >= wy + 5 && mouse_y <= wy + 25) { state = 1; }
            }
        } else if (state == 4) {
            gfx::fill_screen(0x1A1A2E);
            int wx = 200, wy = 150, ww = 400, wh = 300;
            gfx::fill_rect(wx, wy, ww, wh, 0xFFFFFF);
            gfx::fill_rect(wx, wy, ww, 30, 0x333333);
            draw_string("My Device Settings", wx + 10, wy + 10, 0xFFFFFF);
            
            gfx::fill_rect(wx + ww - 30, wy + 5, 20, 20, 0xE74C3C);
            draw_string("X", wx + ww - 25, wy + 10, 0xFFFFFF);

            gfx::fill_rect(wx + 20, wy + 60, 360, 40, use_touch ? 0x4CAF50 : 0xEEEEEE);
            draw_string("Touchscreen", wx + 30, wy + 75, use_touch ? 0xFFFFFF : 0x000000);
            draw_string(use_touch ? "ON" : "OFF", wx + 310, wy + 75, use_touch ? 0xFFFFFF : 0x000000);

            gfx::fill_rect(wx + 20, wy + 120, 360, 40, use_mouse ? 0x4CAF50 : 0xEEEEEE);
            draw_string("Mouse", wx + 30, wy + 135, use_mouse ? 0xFFFFFF : 0x000000);
            draw_string(use_mouse ? "ON" : "OFF", wx + 310, wy + 135, use_mouse ? 0xFFFFFF : 0x000000);

            gfx::fill_rect(wx + 20, wy + 180, 360, 40, use_kb_mouse ? 0x4CAF50 : 0xEEEEEE);
            draw_string("Keyboard Mouse", wx + 30, wy + 195, use_kb_mouse ? 0xFFFFFF : 0x000000);
            draw_string(use_kb_mouse ? "ON" : "OFF", wx + 310, wy + 195, use_kb_mouse ? 0xFFFFFF : 0x000000);

            if (click) {
                if (mouse_x >= wx + ww - 30 && mouse_x <= wx + ww - 10 && mouse_y >= wy + 5 && mouse_y <= wy + 25) { state = 1; }
                
                if (mouse_x >= wx + 20 && mouse_x <= wx + 380 && mouse_y >= wy + 60 && mouse_y <= wy + 100) { use_touch = !use_touch; }
                if (mouse_x >= wx + 20 && mouse_x <= wx + 380 && mouse_y >= wy + 120 && mouse_y <= wy + 160) { use_mouse = !use_mouse; }
                if (mouse_x >= wx + 20 && mouse_x <= wx + 380 && mouse_y >= wy + 180 && mouse_y <= wy + 220) { use_kb_mouse = !use_kb_mouse; }
            }
        }

        if (power_menu_open) {
            gfx::fill_rect(650, 35, 150, 130, 0x333333);
            
            gfx::fill_rect(660, 45, 130, 30, 0x555555);
            draw_string("Settings", 680, 55, 0xFFFFFF);

            gfx::fill_rect(660, 85, 130, 30, 0xE74C3C);
            draw_string("Restart", 690, 95, 0xFFFFFF);
            
            gfx::fill_rect(660, 125, 130, 30, 0xE74C3C);
            draw_string("Shutdown", 690, 135, 0xFFFFFF);

            if (click) {
                if (mouse_x >= 660 && mouse_x <= 790 && mouse_y >= 45 && mouse_y <= 75) { state = 4; power_menu_open = false; }
                if (mouse_x >= 660 && mouse_x <= 790 && mouse_y >= 85 && mouse_y <= 115) { reboot(); }
                if (mouse_x >= 660 && mouse_x <= 790 && mouse_y >= 125 && mouse_y <= 155) { shutdown(); }
                if (mouse_x < 650 || mouse_x > 800 || mouse_y > 165) { power_menu_open = false; }
            }
        }

        draw_cursor(mouse_x, mouse_y);
        gfx::swap();
    }
}
