// ============================================================
//  iMimi OS — Kernel
//  Liquid Glass compositor + CarPlay scaffolding
//  Target: i686, Multiboot 1, framebuffer 800x600x32
// ============================================================

// ---------- базовые типы ----------
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;

// ---------- память ----------
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

// ---------- порты ввода-вывода ----------
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)       { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait()        { outb(0x80, 0); }

// ---------- Multiboot 1 ----------
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

// ---------- подключение splash (сгенерирован tools/img2c.py) ----------
#include "splash.h"

// ============================================================
//  gfx — программный композитор Liquid Glass
// ============================================================
namespace gfx {

    u32* fb     = nullptr;
    int  W      = 800;
    int  H      = 600;
    int  pitch  = 0;

    static u32 back_buffer[800 * 600];

    void init(multiboot_info* mb) {
        if (mb->flags & (1u << 12)) {
            fb     = (u32*)(u32)mb->framebuffer_addr;
            W      = (int)mb->framebuffer_width;
            H      = (int)mb->framebuffer_height;
            pitch  = (int)(mb->framebuffer_pitch / 4);
            if (W <= 0 || W > 800) W = 800;
            if (H <= 0 || H > 600) H = 600;
        }
        memset(back_buffer, 0, sizeof(back_buffer));
    }

    inline void put_pixel(int x, int y, u32 color) {
        if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
            back_buffer[y * W + x] = color;
    }

    inline u32 get_pixel(int x, int y) {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return 0;
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
        fill_rect(x,         y,         w, 1, color);
        fill_rect(x,         y + h - 1, w, 1, color);
        fill_rect(x,         y,         1, h, color);
        fill_rect(x + w - 1, y,         1, h, color);
    }

    // простое box-blur — приближение гауссова размытия
    void blur_region(int rx, int ry, int rw, int rh, int radius) {
        static u32 tmp[800 * 600];
        memcpy(tmp, back_buffer, sizeof(back_buffer));

        for (int y = ry; y < ry + rh; y++) {
            for (int x = rx; x < rx + rw; x++) {
                if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) continue;
                u32 r = 0, g = 0, b = 0, a = 0;
                int count = 0;
                for (int dy = -radius; dy <= radius; dy++) {
                    for (int dx = -radius; dx <= radius; dx++) {
                        int sx = x + dx, sy = y + dy;
                        if ((unsigned)sx >= (unsigned)W || (unsigned)sy >= (unsigned)H) continue;
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

    void blend_rect(int x, int y, int w, int h, u32 color, u8 alpha) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                int px = x + i, py = y + j;
                if ((unsigned)px >= (unsigned)W || (unsigned)py >= (unsigned)H) continue;
                u32 dst = back_buffer[py * W + px];
                u8 dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                u8 sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
                u8 r = (u8)((sr * alpha + dr * (255 - alpha)) / 255);
                u8 g = (u8)((sg * alpha + dg * (255 - alpha)) / 255);
                u8 b = (u8)((sb * alpha + db * (255 - alpha)) / 255);
                back_buffer[py * W + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
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

    // ---------- RGB565 -> RGB888 ----------
    inline u32 rgb565_to_rgb888(u16 c) {
        u8 r = (u8)(((c >> 11) & 0x1F) * 255 / 31);
        u8 g = (u8)(((c >>  5) & 0x3F) * 255 / 63);
        u8 b = (u8)(( c        & 0x1F) * 255 / 31);
        return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | b;
    }

    // ---------- splash: RLE -> framebuffer ----------
    void draw_splash_rle() {
        int ox = (W - (int)splash_w) / 2;
        int oy = (H - (int)splash_h) / 2;
        fill_rect(0, 0, W, H, 0xFF000000);

        u32 idx = 0;
        int x = 0, y = 0;
        while (idx < splash_rle_size) {
            u16 header = splash_rle[idx++];
            if (header & 0x8000) {
                u32 len = header & 0x7FFF;
                u32 col = rgb565_to_rgb888(splash_rle[idx++]);
                for (u32 i = 0; i < len; i++) {
                    put_pixel(ox + x, oy + y, col);
                    if (++x >= (int)splash_w) { x = 0; y++; }
                }
            } else {
                u32 len = header;
                for (u32 i = 0; i < len; i++) {
                    u32 col = rgb565_to_rgb888(splash_rle[idx++]);
                    put_pixel(ox + x, oy + y, col);
                    if (++x >= (int)splash_w) { x = 0; y++; }
                }
            }
        }
    }

    void swap() {
        if (!fb) return;
        u32* dest = fb;
        u32* src  = back_buffer;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++)
                dest[y * pitch + x] = src[y * W + x];
        }
    }
}

// ============================================================
//  CarPlay — каркас подсистемы (iAP2 + MFi точки расширения)
// ============================================================
namespace carplay {
    enum State { DETACHED, USB_CONNECTED, IAP2_HANDSHAKE, SESSION_ACTIVE };
    static State state = DETACHED;
    static u8  iap2_rx_buffer[4096];
    static u32 iap2_rx_len = 0;

    void init() {
        state = DETACHED;
        iap2_rx_len = 0;
    }

    // Вызывается из обработчика USB-прерывания (в будущем)
    void on_usb_packet(const u8* data, u32 len) {
        if (len > sizeof(iap2_rx_buffer)) return;
        memcpy(iap2_rx_buffer, data, len);
        iap2_rx_len = len;
        // Здесь должен быть разбор iAP2 и MFi challenge-response.
        // Без сертифицированного чипа Apple — невозможно.
        if (state == USB_CONNECTED) state = IAP2_HANDSHAKE;
    }

    const char* state_str() {
        switch (state) {
            case DETACHED:       return "CarPlay: detached";
            case USB_CONNECTED:  return "CarPlay: USB connected";
            case IAP2_HANDSHAKE: return "CarPlay: iAP2 handshake (stub)";
            case SESSION_ACTIVE: return "CarPlay: active";
        }
        return "CarPlay: ?";
    }
}

// ============================================================
//  Шрифт 8x8 (ASCII 32..126)
// ============================================================
static const u8 font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x00}, // '!'
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // '#'
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00}, // '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // '/'
    {0x7C,0xC6,0xCE,0xDE,0xF6,0xE6,0x7C,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00}, // '2'
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00}, // '3'
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00}, // '4'
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00}, // '5'
    {0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00}, // '6'
    {0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // '7'
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00}, // '8'
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00}, // '9'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ';'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // '<'
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // '='
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // '>'
    {0x7C,0xC6,0x06,0x1C,0x18,0x00,0x18,0x00}, // '?'
    {0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x78,0x00}, // '@'
    {0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'A'
    {0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00}, // 'B'
    {0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00}, // 'C'
    {0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00}, // 'D'
    {0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00}, // 'E'
    {0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00}, // 'F'
    {0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3E,0x00}, // 'G'
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00}, // 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
    {0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00}, // 'J'
    {0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00}, // 'K'
    {0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00}, // 'L'
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00}, // 'M'
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00}, // 'N'
    {0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'O'
    {0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00}, // 'P'
    {0x78,0xCC,0xCC,0xCC,0xDC,0x78,0x1C,0x00}, // 'Q'
    {0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00}, // 'R'
    {0x78,0xCC,0x60,0x30,0x18,0xCC,0x78,0x00}, // 'S'
    {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'T'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00}, // 'U'
    {0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00}, // 'V'
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00}, // 'W'
    {0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00}, // 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00}, // 'Y'
    {0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00}, // 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // '\\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x78,0x0C,0x7C,0xCC,0x76,0x00}, // 'a'
    {0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00}, // 'b'
    {0x00,0x00,0x78,0xCC,0xC0,0xCC,0x78,0x00}, // 'c'
    {0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00}, // 'd'
    {0x00,0x00,0x78,0xCC,0xFC,0xC0,0x78,0x00}, // 'e'
    {0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00}, // 'f'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8}, // 'g'
    {0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
    {0x06,0x00,0x06,0x06,0x06,0x66,0x66,0x3C}, // 'j'
    {0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00}, // 'm'
    {0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x78,0xCC,0xCC,0xCC,0x78,0x00}, // 'o'
    {0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0}, // 'p'
    {0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E}, // 'q'
    {0x00,0x00,0xDC,0x76,0x66,0x60,0xF0,0x00}, // 'r'
    {0x00,0x00,0x7C,0xC0,0x78,0x0C,0xF8,0x00}, // 's'
    {0x10,0x30,0x7C,0x30,0x30,0x34,0x18,0x00}, // 't'
    {0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00}, // 'u'
    {0x00,0x00,0xCC,0xCC,0xCC,0x78,0x30,0x00}, // 'v'
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, // 'w'
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00}, // 'x'
    {0x00,0x00,0xCC,0xCC,0xCC,0x7C,0x0C,0xF8}, // 'y'
    {0x00,0x00,0xFC,0x98,0x30,0x64,0xFC,0x00}, // 'z'
    {0x1C,0x18,0x18,0xE0,0x18,0x18,0x1C,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0xE0,0x18,0x18,0x1C,0x18,0x18,0xE0,0x00}, // '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // '~'
};

static void draw_char(char c, int x, int y, u32 color) {
    if (c < 32 || c > 126) c = '?';
    const u8* glyph = font8x8[(int)c - 32];
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
//  Точка входа
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic;

    // 1) Инициализация графики
    gfx::init(mb);

    // 2) Splash-экран с картинкой iMimi
    gfx::draw_splash_rle();
    gfx::swap();

    // 3) Пауза ~2 сек (busy-wait; для PIT+IDT — отдельный шаг)
    for (volatile u64 i = 0; i < 300000000ULL; i++)
        asm volatile("pause");

    // 4) Интерфейс Liquid Glass
    gfx::fill_rect(0, 0, gfx::W, gfx::H, 0xFF1A1A2E);

    int px = 100, py = 80, pw = 600, ph = 400;
    if (px + pw > gfx::W) { px = 20; pw = gfx::W - 40; }
    if (py + ph > gfx::H) { py = 20; ph = gfx::H - 40; }

    // размытие под панелью — «стекло»
    gfx::blur_region(px, py, pw, ph, 6);
    gfx::blend_rect(px, py, pw, ph, 0xFFFFFFFF, 40);
    gfx::draw_rect(px, py, pw, ph, 0xFFFFFFFF);

    // блик сверху — «Liquid Glass highlight»
    gfx::gradient_rect(px + 1, py + 1, pw - 2, 80,
                       0xFFFFFFFF, 0x00FFFFFF, 120);

    // текст внутри панели
    draw_string("iMimi OS — Kernel preview",  px + 20, py + 20, 0xFFFFFFFF);
    draw_string("Liquid Glass: active",        px + 20, py + 50, 0xFFCCCCCC);
    draw_string(carplay::state_str(),          px + 20, py + 80, 0xFF88CCFF);
    draw_string("Build: dev / test",           px + 20, py + 110, 0xFFAAAAAA);

    gfx::swap();

    // 5) Idle
    for (;;) asm volatile("hlt");
}
