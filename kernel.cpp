typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

extern "C" void* memcpy(void* d, const void* s, u32 n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d;
}
extern "C" void* memset(void* d, int v, u32 n) {
    u8* a = (u8*)d; while (n--) *a++ = (u8)v; return d;
}
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

// ---------- Структура Multiboot ----------
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

// ---------- Графика ----------
namespace gfx {
    u32* fb = nullptr;
    int W = 1024;
    int H = 768;
    int pitch = 0;

    // Бэкбуфер для устранения мерцания (1024 * 768 * 4 байта = ~3 МБ)
    static u32 back_buffer[1024 * 768];

    void init(multiboot_info* mb) {
        if (mb->flags & (1 << 12)) {
            fb = (u32*)(u32)mb->framebuffer_addr;
            W = mb->framebuffer_width;
            H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch / 4;
        }
    }

    void put_pixel(int x, int y, u32 color) {
        if (x >= 0 && x < W && y >= 0 && y < H) back_buffer[y * W + x] = color;
    }

    void fill_rect(int x, int y, int w, int h, u32 color) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
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

    // Копирование бэкбуфера в видеопамять
    void swap() {
        u32* dest = fb;
        u32* src = back_buffer;
        for (u32 i = 0; i < (u32)(W * H); i++) {
            dest[i] = src[i];
        }
    }

    // Базовый шрифт 8x8
    static const u8 font8x8[96][8] = {
        {0,0,0,0,0,0,0,0}, {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
        {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
        {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
        {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
        {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
        {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
        {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
        {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
        {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
        {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
        {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
        {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
        {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
        {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
        {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
        {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
        {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
        {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
        {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
        {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
        {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
        {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
        {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
        {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
        {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
        {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
        {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
        {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
        {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    };

    void draw_char(int x, int y, char c, u32 color) {
        if (c < 32 || c > 127) return;
        int idx = c - 32;
        for (int j = 0; j < 8; j++) {
            u8 row = font8x8[idx][j];
            for (int i = 0; i < 8; i++) {
                if (row & (1 << i)) put_pixel(x + i, y + j, color);
            }
        }
    }

    void draw_text(int x, int y, const char* str, u32 color) {
        while (*str) {
            draw_char(x, y, *str++, color);
            x += 8;
        }
    }
}

// ---------- Мышь ----------
namespace mouse {
    int x = 512, y = 384;
    u8 btn = 0, pkt[3], idx = 0;

    void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t) {} }
    bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t) {} return t > 0; }
    void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    u8   read()      { return wait_r() ? inb(0x60) : 0; }

    void init() {
        wait_w(); outb(0x64, 0xA8);
        wait_w(); outb(0x64, 0x20);
        u8 st = read(); st &= ~0x20;
        wait_w(); outb(0x64, 0x60);
        wait_w(); outb(0x60, st);
        write(0xF6); read();
        write(0xF4); read();
    }

    bool feed(u8 b) {
        if (idx == 0 && !(b & 8)) return false;
        pkt[idx++] = b;
        if (idx < 3) return false;
        idx = 0;
        
        int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
        int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
        
        x += dx * 2; 
        y -= dy * 2; 

        if (x < 0) x = 0; if (x >= gfx::W) x = gfx::W - 1;
        if (y < 0) y = 0; if (y >= gfx::H) y = gfx::H - 1;

        btn = pkt[0] & 7;
        return true;
    }
}

static void draw_mouse_cursor(int mx, int my) {
    const char* shape[19] = {
        "X           ", "XX          ", "X.X         ", "X..X        ",
        "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
        "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
        "X.X X..X    ", "XX  X..X    ", "X    X..X   ", "     X..X   ",
        "      X..X  ", "      X..X  ", "       XX   "
    };
    for (int j = 0; j < 19; j++) {
        for (int i = 0; i < 12; i++) {
            if (shape[j][i] == 'X') gfx::put_pixel(mx + i, my + j, 0x000000);
            else if (shape[j][i] == '.') gfx::put_pixel(mx + i, my + j, 0xFFFFFF);
        }
    }
}

// ---------- Рабочий стол ----------
static bool showWelcome = true; // Флаг отображения приветственного окна

static void draw_desktop() {
    // 1. Фон
    gfx::fill_rect(0, 0, gfx::W, gfx::H, 0x1A3C6C);

    // 2. Панель задач (сверху)
    gfx::fill_rect(0, 0, gfx::W, 40, 0x2D2D2D);
    gfx::draw_text(20, 16, "MinimalZero OS v0.4 (Stable)", 0xFFFFFF);

    // 3. Иконки
    gfx::fill_rect(50, 100, 64, 64, 0xF0C040);
    gfx::draw_text(50, 170, "Files", 0xFFFFFF);
    
    gfx::fill_rect(50, 220, 64, 64, 0x40C0F0);
    gfx::draw_text(50, 290, "Trash", 0xFFFFFF);

    // 4. Приветственное окно (открывается при старте)
    if (showWelcome) {
        int wx = 262, wy = 184, ww = 500, wh = 400; // По центру
        
        // Тень
        gfx::fill_rect(wx + 10, wy + 10, ww, wh, 0x0A0A0A);
        // Фон окна
        gfx::fill_rect(wx, wy, ww, wh, 0xF0F0F0);
        // Заголовок
        gfx::fill_rect(wx, wy, ww, 30, 0x0078D7);
        gfx::draw_rect(wx, wy, ww, wh, 0x808080);
        gfx::draw_text(wx + 10, wy + 11, "Welcome to MinimalZero", 0xFFFFFF);
        
        // Кнопка закрытия (крестик)
        gfx::fill_rect(wx + ww - 25, wy + 5, 20, 20, 0xE81123);
        // Рисуем крестик
        for (int i = 0; i < 10; i++) {
            gfx::put_pixel(wx + ww - 20 + i, wy + 8 + i, 0xFFFFFF);
            gfx::put_pixel(wx + ww - 20 + i, wy + 17 - i, 0xFFFFFF);
        }

        // Текст приветствия
        gfx::draw_text(wx + 20, wy + 60, "Welcome, user!", 0x000000);
        gfx::draw_text(wx + 20, wy + 90, "This is your new graphical environment.", 0x333333);
        gfx::draw_text(wx + 20, wy + 110, "Features:", 0x000000);
        
        gfx::draw_text(wx + 20, wy + 140, "- Smooth 1024x768 Graphics", 0x444444);
        gfx::draw_text(wx + 20, wy + 160, "- Fully working mouse (double buffered)", 0x444444);
        gfx::draw_text(wx + 20, wy + 180, "- Built-in 8x8 font", 0x444444);
        gfx::draw_text(wx + 20, wy + 200, "- Base for File Manager & Trash", 0x444444);

        gfx::draw_text(wx + 20, wy + 250, "How to start:", 0x000000);
        gfx::draw_text(wx + 20, wy + 280, "Click the [X] button in the top right corner", 0x444444);
        gfx::draw_text(wx + 20, wy + 300, "of this window to close it and explore.", 0x444444);
        
        // Кнопка "OK"
        int btnX = wx + ww / 2 - 50, btnY = wy + 340, btnW = 100, btnH = 30;
        gfx::fill_rect(btnX, btnY, btnW, btnH, 0x0078D7);
        gfx::draw_rect(btnX, btnY, btnW, btnH, 0x005A9E);
        gfx::draw_text(btnX + 35, btnY + 11, "Got it!", 0xFFFFFF);
    }
}

extern "C" void kmain(u32 magic, multiboot_info* mb) {
    if (magic != 0x2BADB002) for(;;);
    gfx::init(mb);
    mouse::init();
    
    u8 prev_btn = 0;
    
    for (;;) {
        // Отрисовка кадра в бэкбуфер
        draw_desktop();
        draw_mouse_cursor(mouse::x, mouse::y);
        
        // Мгновенное копирование готового кадра на экран (без мерцания)
        gfx::swap();
        
        // Обработка кликов мыши
        u8 current_btn = mouse::btn;
        if ((current_btn & 1) && !(prev_btn & 1)) { // Левый клик
            int mx = mouse::x;
            int my = mouse::y;
            
            if (showWelcome) {
                int wx = 262, wy = 184, ww = 500;
                // Клик по крестику [X]
                if (mx >= wx + ww - 25 && mx <= wx + ww - 5 && my >= wy + 5 && my <= wy + 25) {
                    showWelcome = false;
                }
                // Клик по кнопке "Got it!"
                int btnX = wx + ww / 2 - 50, btnY = wy + 340;
                if (mx >= btnX && mx <= btnX + 100 && my >= btnY && my <= btnY + 30) {
                    showWelcome = false;
                }
            }
        }
        prev_btn = current_btn;
        
        // Опрос портов
        u8 st = inb(0x64);
        if (st & 1) {
            u8 b = inb(0x60);
            if (st & 0x20) mouse::feed(b);
        }
        
        // Небольшая задержка для стабильности
        for (volatile int i = 0; i < 100000; i++); 
    }
}
