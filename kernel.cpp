typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

// ---------- Базовая математика и память ----------
extern "C" void* memcpy(void* d, const void* s, u32 n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d;
}
extern "C" void* memset(void* d, int v, u32 n) {
    u8* a = (u8*)d; while (n--) *a++ = (u8)v; return d;
}
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

// ---------- Графический драйвер (VESA) ----------
namespace gfx {
    u32* fb = (u32*)0xFD000000; // Адрес видеопамяти VESA LFB
    const int W = 1024;
    const int H = 768;

    void put_pixel(int x, int y, u32 color) {
        if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = color;
    }

    void fill_rect(int x, int y, int w, int h, u32 color) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > W) w = W - x;
        if (y + h > H) h = H - y;
        for (int j = 0; j < h; j++) {
            u32* row = &fb[(y + j) * W + x];
            for (int i = 0; i < w; i++) row[i] = color;
        }
    }

    void draw_rect(int x, int y, int w, int h, u32 color) {
        fill_rect(x, y, w, 1, color);       // верх
        fill_rect(x, y + h - 1, w, 1, color); // низ
        fill_rect(x, y, 1, h, color);       // лево
        fill_rect(x + w - 1, y, 1, h, color); // право
    }
}

// ---------- Мышь PS/2 ----------
namespace mouse {
    int x = 512, y = 384; // Центр экрана
    int ax = 0, ay = 0, idx = 0;
    u8 btn = 0, pkt[3];

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
        
        // Масштабируем скорость мыши под 1024x768
        x += dx * 2; 
        y -= dy * 2; 

        if (x < 0) x = 0; if (x >= gfx::W) x = gfx::W - 1;
        if (y < 0) y = 0; if (y >= gfx::H) y = gfx::H - 1;

        btn = pkt[0] & 7;
        return true;
    }
}

// ---------- Рисование курсора мыши ----------
// Рисуем классическую стрелочку 12x19
static void draw_mouse_cursor(int mx, int my) {
    const char* cursor_shape[19] = {
        "X           ",
        "XX          ",
        "X.X         ",
        "X..X        ",
        "X...X       ",
        "X....X      ",
        "X.....X     ",
        "X......X    ",
        "X.......X   ",
        "X........X  ",
        "X.....XXXXX ",
        "X..X..X     ",
        "X.X X..X    ",
        "XX  X..X    ",
        "X    X..X   ",
        "     X..X   ",
        "      X..X  ",
        "      X..X  ",
        "       XX   "
    };
    for (int j = 0; j < 19; j++) {
        for (int i = 0; i < 12; i++) {
            if (cursor_shape[j][i] == 'X') gfx::put_pixel(mx + i, my + j, 0x000000); // Черный контур
            else if (cursor_shape[j][i] == '.') gfx::put_pixel(mx + i, my + j, 0xFFFFFF); // Белая заливка
        }
    }
}

// ---------- Рабочий стол ----------
static void draw_desktop() {
    // 1. Фон (приятный синий градиент/цвет)
    gfx::fill_rect(0, 0, gfx::W, gfx::H, 0x1A3C6C);

    // 2. Панель задач (сверху)
    gfx::fill_rect(0, 0, gfx::W, 40, 0x2D2D2D);
    gfx::draw_rect(0, 0, gfx::W, 40, 0x555555);

    // 3. Окно "Files" (просто для примера)
    int wx = 100, wy = 150, ww = 500, wh = 400;
    
    // Тень окна
    gfx::fill_rect(wx + 5, wy + 5, ww, wh, 0x0A0A0A);
    // Фон окна
    gfx::fill_rect(wx, wy, ww, wh, 0xE0E0E0);
    // Заголовок окна
    gfx::fill_rect(wx, wy, ww, 30, 0x3E7BFA);
    // Рамка окна
    gfx::draw_rect(wx, wy, ww, wh, 0x808080);
    
    // Кнопка закрытия (крестик)
    gfx::fill_rect(wx + ww - 25, wy + 5, 20, 20, 0xE81123);
    gfx::fill_rect(wx + ww - 20, wy + 8, 10, 2, 0xFFFFFF);
    gfx::fill_rect(wx + ww - 20, wy + 15, 10, 2, 0xFFFFFF);

    // 4. Иконки на рабочем столе
    gfx::fill_rect(50, 100, 64, 64, 0xF0C040); // Иконка "Files"
    gfx::fill_rect(50, 200, 64, 64, 0x40C0F0); // Иконка "Trash"
}

// ---------- Точка входа ----------
extern "C" void kmain() {
    mouse::init();
    
    // Основной цикл
    for (;;) {
        draw_desktop();             // Отрисовка фона и окон
        draw_mouse_cursor(mouse::x, mouse::y); // Отрисовка курсора поверх
        
        // Опрос мыши
        u8 st = inb(0x64);
        if (st & 1) {
            u8 b = inb(0x60);
            if (st & 0x20) mouse::feed(b); // Если это мышь
        }
        
        // Небольшая задержка, чтобы не грузить CPU на 100%
        for (volatile int i = 0; i < 100000; i++); 
    }
}
