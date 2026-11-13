// kernel.cpp — iMimi Kernel (Liquid Glass + CarPlay scaffolding)
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

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

static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8  inb(u16 p) { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

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

// ============================================================
//  GFX: фреймбуфер + программный композитор (Liquid Glass)
// ============================================================
namespace gfx {
    u32* fb      = nullptr;
    int  W       = 800;
    int  H       = 600;
    int  pitch   = 0;
    static u32 back_buffer[800 * 600];

    void init(multiboot_info* mb) {
        if (mb->flags & (1 << 12)) {
            fb    = (u32*)(u32)mb->framebuffer_addr;
            W     = mb->framebuffer_width;
            H     = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch / 4;
        }
    }

    void put_pixel(int x, int y, u32 color) {
        if (x >= 0 && x < W && y >= 0 && y < H)
            back_buffer[y * W + x] = color;
    }

    u32 get_pixel(int x, int y) {
        if (x < 0 || x >= W || y < 0 || y >= H) return 0;
        return back_buffer[y * W + x];
    }

    void fill_rect(int x, int y, int w, int h, u32 color) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
        if (w <= 0 || h <= 0) return;
        for (int j = 0; j < h; j++) {
            u32* row = &back_buffer[(y + j) * W + x];
            for (int i = 0; i < w; i++) row[i] = color;
        }
    }

    void draw_rect(int x, int y, int w, int h, u32 color) {
        fill_rect(x, y, w, 1, color);
        fill_rect(x, y + h - 1, w, 1, color);
        fill_rect(x, y, 1, h, color);
        fill_rect(x + w - 1, y, 1, h, color);
    }

    // ---------- Liquid Glass helpers ----------
    // Простое box-blur (приближение гауссова размытия)
    void blur_region(int rx, int ry, int rw, int rh, int radius) {
        static u32 tmp[800 * 600];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                tmp[y * W + x] = back_buffer[y * W + x];

        for (int y = ry; y < ry + rh; y++) {
            for (int x = rx; x < rx + rw; x++) {
                if (x < 0 || x >= W || y < 0 || y >= H) continue;
                u32 r = 0, g = 0, b = 0, a = 0;
                int count = 0;
                for (int dy = -radius; dy <= radius; dy++) {
                    for (int dx = -radius; dx <= radius; dx++) {
                        int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
                        u32 c = tmp[sy * W + sx];
                        r += (c >> 16) & 0xFF;
                        g += (c >>  8) & 0xFF;
                        b +=  c        & 0xFF;
                        a += (c >> 24) & 0xFF;
                        count++;
                    }
                }
                if (count) {
                    r /= count; g /= count; b /= count; a /= count;
                    back_buffer[y * W + x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }

    // Альфа-смешивание поверх фона (эмуляция прозрачности стекла)
    void blend_rect(int x, int y, int w, int h, u32 color, u8 alpha) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                int px = x + i, py = y + j;
                if (px < 0 || px >= W || py < 0 || py >= H) continue;
                u32 dst = back_buffer[py * W + px];
                u8 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                u8 sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
                u8 r = (sr * alpha + dr * (255 - alpha)) / 255;
                u8 g = (sg * alpha + dg * (255 - alpha)) / 255;
                u8 b = (sb * alpha + db * (255 - alpha)) / 255;
                back_buffer[py * W + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    // Вертикальный градиент — для бликов «жидкого стекла»
    void gradient_rect(int x, int y, int w, int h, u32 top, u32 bottom, u8 alpha) {
        for (int j = 0; j < h; j++) {
            u8 t = (u8)((j * 255) / (h > 1 ? h - 1 : 1));
            u8 r = ((top >> 16) & 0xFF) * (255 - t) / 255 + ((bottom >> 16) & 0xFF) * t / 255;
            u8 g = ((top >>  8) & 0xFF) * (255 - t) / 255 + ((bottom >>  8) & 0xFF) * t / 255;
            u8 b = ( top        & 0xFF) * (255 - t) / 255 + ( bottom        & 0xFF) * t / 255;
            blend_rect(x, y + j, w, 1, (r << 16) | (g << 8) | b, alpha);
        }
    }

    void swap() {
        u32* dest = fb;
        u32* src  = back_buffer;
        for (u32 i = 0; i < (u32)(W * H); i++) dest[i] = src[i];
    }
}

// ============================================================
//  CarPlay: каркас подсистемы (без проприетарного стека)
// ============================================================
namespace carplay {
    // В реальной системе здесь был бы iAP2 + MFi. Пока — только транспорт.
    enum State { DETACHED, USB_CONNECTED, IAP2_HANDSHAKE, SESSION_ACTIVE };
    static State state = DETACHED;
    static u8  iap2_rx_buffer[4096];
    static u32 iap2_rx_len = 0;

    void init() {
        state = DETACHED;
        iap2_rx_len = 0;
    }

    // Заглушка: вызывается из обработчика USB-прерывания (в будущем)
    void on_usb_packet(const u8* data, u32 len) {
        if (len > sizeof(iap2_rx_buffer)) return;
        memcpy(iap2_rx_buffer, data, len);
        iap2_rx_len = len;
        // Здесь должен быть разбор iAP2-заголовка 0xFF 0x5A и MFi challenge-response.
        // Без сертифицированного чипа это невозможно — оставлено как точка расширения.
        if (state == USB_CONNECTED) state = IAP2_HANDSHAKE;
    }

    const char* state_str() {
        switch (state) {
            case DETACHED:       return "CarPlay: detached";
            case USB_CONNECTED:  return "CarPlay: USB connected";
            case IAP2_HANDSHAKE: return "CarPlay: iAP2 handshake (stub)";
            case SESSION_ACTIVE: return "CarPlay: active";
        }
        return "?";
    }
}

// ============================================================
//  Простой шрифт 8x8 (оставлен из MinimalZero для вывода текста)
// ============================================================
static const u8 font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    // ... (остальные символы можно оставить как в MinimalZero, здесь сокращено для читаемости)
};

static void draw_char(char c, int x, int y, u32 color) {
    if (c < 32 || c > 127) c = '?';
    const u8* glyph = font8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        u8 bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << (7 - col)))
                gfx::put_pixel(x + col, y + row, color);
        }
    }
}

static void draw_string(const char* s, int x, int y, u32 color) {
    while (*s) {
        draw_char(*s++, x, y, color);
        x += 8;
    }
}

// ============================================================
//  kmain — точка входа
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic;
    gfx::init(mb);

    // Фон: тёмный градиент — база для Liquid Glass
    gfx::fill_rect(0, 0, gfx::W, gfx::H, 0xFF1A1A2E);

    // Пример «стеклянной» панели: размытие фона под ней + полупрозрачный слой
    int panelX = 100, panelY = 80, panelW = 600, panelH = 400;
    gfx::blur_region(panelX, panelY, panelW, panelH, 6);
    gfx::blend_rect(panelX, panelY, panelW, panelH, 0xFFFFFFFF, 40); // белая дымка
    gfx::draw_rect(panelX, panelY, panelW, panelH, 0xFFFFFFFF);

    // Блик сверху панели (Liquid Glass highlight)
    gfx::gradient_rect(panelX + 1, panelY + 1, panelW - 2, 80,
                       0xFFFFFFFF, 0x00FFFFFF, 120);

    // Текст
    draw_string("iMimi OS — Kernel preview", panelX + 20, panelY + 20, 0xFFFFFFFF);
    draw_string("Liquid Glass compositor: active", panelX + 20, panelY + 50, 0xFFCCCCCC);
    draw_string(carplay::state_str(), panelX + 20, panelY + 80, 0xFF88CCFF);

    gfx::swap();

    // Бесконечный цикл (в реальной ОС здесь планировщик)
    while (1) {
        asm volatile("hlt");
    }
}
