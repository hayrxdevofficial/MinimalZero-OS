// ============================================================
//  iMimi OS — Mimi Clicker (White Edition)
// ============================================================

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)       { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait()        { outb(0x80, 0); }

struct multiboot_info {
    u32 flags; u32 mem_lower, mem_upper; u32 boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4]; u32 mmap_length, mmap_addr; u32 drives_length, drives_addr;
    u32 config_table, boot_loader_name, apm_table; u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr; u32 framebuffer_pitch; u32 framebuffer_width; u32 framebuffer_height;
    u8  framebuffer_bpp; u8  framebuffer_type;
} __attribute__((packed));

#include "images.h"

// --- IDT И ПРЕРЫВАНИЯ ---
struct IDT_entry { u16 base_low; u16 sel; u8 zero; u8 flags; u16 base_high; } __attribute__((packed));
struct IDT_ptr { u16 limit; u32 base; } __attribute__((packed));
IDT_entry idt[256];
IDT_ptr idtp;
extern "C" void isr12();
extern "C" void load_idt(u32);

int mouse_x = 400, mouse_y = 300;
u8 mouse_cycle = 0;
u8 mouse_byte[3];
bool mouse_left_clicked = false;

extern "C" void mouse_handler_c() {
    u8 status = inb(0x64);
    if (!(status & 0x20)) return;
    u8 data = inb(0x60);
    
    switch(mouse_cycle) {
        case 0: mouse_byte[0] = data; mouse_cycle++; break;
        case 1: mouse_byte[1] = data; mouse_cycle++; break;
        case 2: 
            mouse_byte[2] = data; mouse_cycle = 0;
            if (mouse_byte[0] & 0x10) mouse_x -= (256 - mouse_byte[1]); else mouse_x += mouse_byte[1];
            if (mouse_byte[0] & 0x20) mouse_y -= (256 - mouse_byte[2]); else mouse_y += mouse_byte[2];
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x > 799) mouse_x = 799;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > 599) mouse_y = 599;
            
            if (mouse_byte[0] & 0x01) mouse_left_clicked = true;
            break;
    }
    outb(0xA0, 0x20); outb(0x20, 0x20);
}

asm(".global isr12\n isr12:\n pusha\n call mouse_handler_c\n popa\n iret\n");

void init_idt() {
    idtp.limit = (sizeof(IDT_entry) * 256) - 1; 
    idtp.base = (u32)&idt;
    for(int i = 0; i < 256; i++) { 
        idt[i].base_low = 0; idt[i].sel = 0; idt[i].zero = 0; idt[i].flags = 0; idt[i].base_high = 0; 
    }
    u32 base = (u32)isr12;
    idt[0x2C].base_low = base & 0xFFFF; 
    idt[0x2C].sel = 0x08; 
    idt[0x2C].flags = 0x8E; 
    idt[0x2C].base_high = (base >> 16) & 0xFFFF;
    load_idt((u32)&idtp);
}

void init_pic() {
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0x00); outb(0xA1, 0x00); outb(0x21, 0xF7); outb(0xA1, 0xEF);
}

void init_mouse() {
    outb(0x64, 0xA8); outb(0x64, 0x20); io_wait();
    u8 status = inb(0x60) | 2; outb(0x64, 0x60); io_wait();
    outb(0x60, status); outb(0x64, 0xD4); outb(0x60, 0xF4);
}

namespace gfx {
    static u8* fb = nullptr; static u32 W = 800, H = 600, pitch = 0; static u8 bpp = 32;
    void init(multiboot_info* mb) {
        if (mb->flags & (1u << 12)) {
            fb = (u8*)(u32)mb->framebuffer_addr; W = mb->framebuffer_width; H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch; bpp = mb->framebuffer_bpp;
        }
    }
    inline void put_pixel(u32 x, u32 y, u32 color) {
        if (x >= W || y >= H || !fb) return;
        if (bpp == 32) *(u32*)(fb + y * pitch + x * 4) = color;
    }
    void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
        for (u32 j = 0; j < h; j++) for (u32 i = 0; i < w; i++) put_pixel(x + i, y + j, color);
    }
    void fill_screen(u32 color) { fill_rect(0, 0, W, H, color); }

    void draw_circle(int cx, int cy, int r, u32 color) {
        int x = r, y = 0, err = 0;
        while (x >= y) {
            put_pixel(cx + x, cy + y, color); put_pixel(cx + y, cy + x, color);
            put_pixel(cx - y, cy + x, color); put_pixel(cx - x, cy + y, color);
            put_pixel(cx - x, cy - y, color); put_pixel(cx - y, cy - x, color);
            put_pixel(cx + y, cy - x, color); put_pixel(cx + x, cy - y, color);
            if (err <= 0) { y += 1; err += 2*y + 1; }
            if (err > 0) { x -= 1; err -= 2*x + 1; }
        }
    }

    inline u32 rgb565_to_888(u16 c) {
        u8 r = ((c >> 11) & 0x1F) * 255 / 31; u8 g = ((c >> 5) & 0x3F) * 255 / 63; u8 b = (c & 0x1F) * 255 / 31;
        return (r << 16) | (g << 8) | b;
    }

    void draw_image_centered(int target_h, const u16* rle, u32 rle_size, int img_w, int img_h, int squish_percent) {
        int draw_h = (target_h * squish_percent) / 100;
        int draw_w = (img_w * draw_h) / img_h; 
        
        int ox = (W - draw_w) / 2;
        int oy = (H - draw_h) / 2 - 30;

        u32 idx = 0; int x = 0, y = 0;
        while (idx < rle_size) {
            u16 header = rle[idx++];
            if (header & 0x8000) {
                u32 len = header & 0x7FFF;
                u32 col = rgb565_to_888(rle[idx++]);
                for (u32 i = 0; i < len; i++) {
                    int px = ox + (x * draw_w) / img_w;
                    int py = oy + (y * draw_h) / img_h;
                    put_pixel(px, py, col);
                    if (++x >= img_w) { x = 0; y++; }
                }
            } else {
                u32 len = header;
                for (u32 i = 0; i < len; i++) {
                    u32 col = rgb565_to_888(rle[idx++]);
                    int px = ox + (x * draw_w) / img_w;
                    int py = oy + (y * draw_h) / img_h;
                    put_pixel(px, py, col);
                    if (++x >= img_w) { x = 0; y++; }
                }
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

void draw_string(const char* s, int x, int y, u32 color) {
    while (*s) {
        char c = *s++; if (c < 32 || c > 126) c = '?';
        const u8* g = font8x8[c - 32];
        for (int r = 0; r < 8; r++) for (int cl = 0; cl < 8; cl++)
            if (g[r] & (1 << (7 - cl))) gfx::put_pixel(x + cl, y + r, color);
        x += 8;
    }
}

// ============================================================
//  ОСНОВНАЯ ЛОГИКА
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic; gfx::init(mb); init_idt(); init_pic(); init_mouse(); asm volatile("sti");

    int state = 0; 
    int current_mimi = 0; 
    int clicker_squish = 100; 
    int clicker_dir = 0;     

    while (1) {
        bool click = mouse_left_clicked; mouse_left_clicked = false;

        if (state == 0) {
            gfx::fill_screen(0xFFFFFF); 
            gfx::draw_image_centered(300, img_base_rle, img_base_size, img_base_w, img_base_h, 100);
            gfx::draw_circle(400, 500, 20, 0xDDDDDD);
            for(volatile int i=0; i<100000000; i++);
            state = 1; gfx::fill_screen(0x1A1A2E); 
        } 
        else if (state == 1) {
            gfx::fill_rect(50, 50, 80, 80, 0x3498DB); 
            draw_string("Mimi", 55, 85, 0xFFFFFF);
            if (click && mouse_x >= 50 && mouse_x <= 130 && mouse_y >= 50 && mouse_y <= 130) state = 2;
        } 
        else if (state == 2) {
            gfx::fill_screen(0xFFFFFF); 
            
            draw_string("Mimi Clicker", 350, 30, 0x000000);
            
            gfx::fill_rect(50, 520, 150, 40, 0xEEEEEE);
            draw_string("Mini", 100, 535, 0x000000);
            gfx::fill_rect(250, 520, 150, 40, 0xEEEEEE);
            draw_string("Adult", 290, 535, 0x000000);
            gfx::fill_rect(450, 520, 150, 40, 0xEEEEEE);
            draw_string("Cool", 500, 535, 0x000000);

            if (click) {
                if (mouse_x >= 50 && mouse_x <= 200 && mouse_y >= 520 && mouse_y <= 560) current_mimi = 0;
                if (mouse_x >= 250 && mouse_x <= 400 && mouse_y >= 520 && mouse_y <= 560) current_mimi = 1;
                if (mouse_x >= 450 && mouse_x <= 600 && mouse_y >= 520 && mouse_y <= 560) current_mimi = 2;
                if (mouse_x < 40 && mouse_y < 40) state = 1;
            }

            int ox = 0, oy = 0, draw_w = 0, draw_h = 0;
            if (current_mimi == 0) {
                draw_h = 250 * clicker_squish / 100;
                draw_w = img_base_w * draw_h / img_base_h;
                gfx::draw_image_centered(250, img_base_rle, img_base_size, img_base_w, img_base_h, clicker_squish);
            } else if (current_mimi == 1) {
                draw_h = 250 * clicker_squish / 100;
                draw_w = img_adult_w * draw_h / img_adult_h;
                gfx::draw_image_centered(250, img_adult_rle, img_adult_size, img_adult_w, img_adult_h, clicker_squish);
            } else {
                draw_h = 250 * clicker_squish / 100;
                draw_w = img_cool_w * draw_h / img_cool_h;
                gfx::draw_image_centered(250, img_cool_rle, img_cool_size, img_cool_w, img_cool_h, clicker_squish);
            }

            ox = (800 - draw_w) / 2;
            oy = (600 - draw_h) / 2 - 30;

            if (click && mouse_x >= ox && mouse_x <= ox + draw_w && mouse_y >= oy && mouse_y <= oy + draw_h) {
                clicker_squish = 80; 
                clicker_dir = 1;    
            }

            if (clicker_dir == 1) {
                clicker_squish += 1;
                if (clicker_squish >= 100) { clicker_squish = 100; clicker_dir = 0; }
                for(volatile int i=0; i<2000000; i++); 
            }
        }

        gfx::fill_rect(mouse_x, mouse_y, 4, 4, 0xFF0000);
    }
}
