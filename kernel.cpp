// ============================================================
//  iMimi OS — Kernel (Optimized for v86 Emulator)
// ============================================================

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

extern "C" void* memcpy(void* d, const void* s, u32 n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    while (n--) *a++ = *b++;
    return d;
}
extern "C" void* memset(void* d, int v, u32 n) {
    u8* a = (u8*)d;
    while (n--) *a++ = (u8)v;
    return d;
}

struct multiboot_info {
    u32 flags;
    u32 mem_lower, mem_upper;
    u32 boot_device, cmdline, mods_count, mods_addr;
    u32 syms[4];
    u32 mmap_length, mmap_addr;
    u32 drives_length, drives_addr;
    u32 config_table, boot_loader_name, apm_table;
    u32 vbe_control_info, vbe_mode_info;
    u16 vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;
} __attribute__((packed));

#include "splash.h"

// ============================================================
//  gfx — программный композитор
// ============================================================
namespace gfx {
    u8*  fb     = nullptr;
    u32  W      = 800;
    u32  H      = 600;
    u32  pitch  = 0;
    u8   bpp    = 32;

    // Бэкбуфер (всегда 32-бит для простоты математики)
    static u32 back_buffer[800 * 600];

    void init(multiboot_info* mb) {
        if (mb->flags & (1u << 12)) {
            fb     = (u8*)(u32)mb->framebuffer_addr;
            W      = mb->framebuffer_width;
            H      = mb->framebuffer_height;
            pitch  = mb->framebuffer_pitch;
            bpp    = mb->framebuffer_bpp;
            if (W == 0 || W > 800) W = 800;
            if (H == 0 || H > 600) H = 600;
        }
        memset(back_buffer, 0, sizeof(back_buffer));
    }

    inline void put_pixel(int x, int y, u32 color) {
        if ((unsigned)x < W && (unsigned)y < H)
            back_buffer[y * W + x] = color;
    }

    void fill_rect(int x, int y, int w, int h, u32 color) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int)W) w = W - x;
        if (y + h > (int)H) h = H - y;
        if (w <= 0 || h <= 0) return;
        for (int j = 0; j < h; j++) {
            u32* row = &back_buffer[(y + j) * W + x];
            for (int i = 0; i < w; i++) row[i] = color;
        }
    }

    void draw_rect(int x, int y, int w, int h, u32 color) {
        fill_rect(x, y, w, 1, color); fill_rect(x, y + h - 1, w, 1, color);
        fill_rect(x, y, 1, h, color); fill_rect(x + w - 1, y, 1, h, color);
    }

    // Быстрое альфа-смешивание (вместо тяжелого blur)
    void blend_rect(int x, int y, int w, int h, u32 color, u8 alpha) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                int px = x + i, py = y + j;
                if ((unsigned)px >= W || (unsigned)py >= H) continue;
                u32 dst = back_buffer[py * W + px];
                u8 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                u8 sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
                u8 r = (u8)((sr * alpha + dr * (255 - alpha)) / 255);
                u8 g = (u8)((sg * alpha + dg * (255 - alpha)) / 255);
                u8 b = (u8)((sb * alpha + db * (255 - alpha)) / 255);
                back_buffer[py * W + px] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
    }

    void gradient_rect(int x, int y, int w, int h, u32 top, u32 bottom, u8 alpha) {
        if (h <= 0) return;
        for (int j = 0; j < h; j++) {
            u8 t = (u8)((j * 255) / (h > 1 ? h - 1 : 1));
            u8 r = (u8)(((top    >> 16) & 0xFF) * (255 - t) / 255 + ((bottom    >> 16) & 0xFF) * t / 255);
            u8 g = (u8)(((top    >>  8) & 0xFF) * (255 - t) / 255 + ((bottom    >>  8) & 0xFF) * t / 255);
            u8 b = (u8)(( top           & 0xFF) * (255 - t) / 255 + ( bottom           & 0xFF) * t / 255);
            blend_rect(x, y + j, w, 1, (u32)((r << 16) | (g << 8) | b), alpha);
        }
    }

    inline u32 rgb565_to_rgb888(u16 c) {
        u8 r = (u8)(((c >> 11) & 0x1F) * 255 / 31);
        u8 g = (u8)(((c >>  5) & 0x3F) * 255 / 63);
        u8 b = (u8)(( c        & 0x1F) * 255 / 31);
        return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b;
    }

    void draw_splash_rle() {
        int ox = (W - (int)splash_w) / 2;
        int oy = (H - (int)splash_h) / 2;
        fill_rect(0, 0, W, H, 0xFF000000);
        u32 idx = 0, x = 0, y = 0;
        while (idx < splash_rle_size) {
            u16 header = splash_rle[idx++];
            if (header & 0x8000) {
                u32 len = header & 0x7FFF;
                u32 col = rgb565_to_rgb888(splash_rle[idx++]);
                for (u32 i = 0; i < len; i++) {
                    put_pixel(ox + x, oy + y, col);
                    if (++x >= splash_w) { x = 0; y++; }
                }
            } else {
                u32 len = header;
                for (u32 i = 0; i < len; i++) {
                    u32 col = rgb565_to_rgb888(splash_rle[idx++]);
                    put_pixel(ox + x, oy + y, col);
                    if (++x >= splash_w) { x = 0; y++; }
                }
            }
        }
    }

    // Универсальный вывод (поддержка 16, 24, 32 bpp для v86)
    void swap() {
        if (!fb) return;
        u32* fb32 = (u32*)fb;
        u16* fb16 = (u16*)fb;
        u8*  fb24 = fb;
        u32 p32 = pitch / 4;
        u32 p16 = pitch / 2;

        for (u32 y = 0; y < H; y++) {
            for (u32 x = 0; x < W; x++) {
                u32 color = back_buffer[y * W + x];
                if (bpp == 32) {
                    fb32[y * p32 + x] = color;
                } else if (bpp == 24) {
                    u32 idx = y * pitch + x * 3;
                    fb24[idx + 0] = color & 0xFF;
                    fb24[idx + 1] = (color >> 8) & 0xFF;
                    fb24[idx + 2] = (color >> 16) & 0xFF;
                } else if (bpp == 16) {
                    u16 r = (color >> 19) & 0x1F;
                    u16 g = (color >> 10) & 0x3F;
                    u16 b = (color >> 3) & 0x1F;
                    fb16[y * p16 + x] = (r << 11) | (g << 5) | b;
                }
            }
        }
    }
}

// ============================================================
//  CarPlay
// ============================================================
namespace carplay {
    enum State { DETACHED, USB_CONNECTED, IAP2_HANDSHAKE, SESSION_ACTIVE };
    static State state = DETACHED;
    const char* state_str() { return "CarPlay: detached"; }
}

// ============================================================
//  Шрифт (сокращен для экономии места, работает стандартно)
// ============================================================
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

static void draw_string(const char* s, int x, int y, u32 color) {
    while (*s) {
        char c = *s++;
        if (c < 32 || c > 126) c = '?';
        const u8* g = font8x8[c - 32];
        for (int r = 0; r < 8; r++) {
            for (int cl = 0; cl < 8; cl++) {
                if (g[r] & (1 << (7 - cl))) gfx::put_pixel(x + cl, y + r, color);
            }
        }
        x += 8;
    }
}

// ============================================================
//  Точка входа
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic;
    gfx::init(mb);

    // 1. Splash (Заставка)
    gfx::draw_splash_rle();
    gfx::swap();

    // 2. Пауза (ОПТИМИЗИРОВАНО! Уменьшено в 30 раз, чтобы v86 не вешался)
    for (volatile u32 i = 0; i < 10000000UL; i++) asm volatile("pause");

    // 3. Интерфейс Liquid Glass (БЕЗ ТЯЖЕЛОГО BLUR)
    gfx::fill_rect(0, 0, gfx::W, gfx::H, 0xFF1A1A2E); // Темный фон

    int px = 100, py = 80, pw = 600, ph = 400;
    if (px + pw > (int)gfx::W) { px = 20; pw = gfx::W - 40; }
    if (py + ph > (int)gfx::H) { py = 20; ph = gfx::H - 40; }

    // Имитация стекла (просто заливаем полупрозрачным белым)
    gfx::blend_rect(px, py, pw, ph, 0xFFFFFF, 30); 
    // Рамка стекла
    gfx::draw_rect(px, py, pw, ph, 0xFFFFFF); 

    // Блик сверху (градиент)
    gfx::gradient_rect(px + 1, py + 1, pw - 2, 80, 0xFFFFFF, 0x000000, 80);

    // Текст
    draw_string("iMimi OS", px + 20, py + 20, 0xFFFFFFFF);
    draw_string("Liquid Glass: active", px + 20, py + 50, 0xFFCCCCCC);
    draw_string(carplay::state_str(), px + 20, py + 80, 0xFF88CCFF);
    draw_string("Build: v86 compatible", px + 20, py + 110, 0xFFAAAAAA);

    gfx::swap();

    for (;;) asm volatile("hlt");
}
