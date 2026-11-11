// MinimalZero 0.2: ядро, драйверы (VGA, клавиатура, мышь, RTC) и рабочий стол на C++
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern "C" void* memcpy(void* d, const void* s, u32 n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d;
}
extern "C" void* memset(void* d, int v, u32 n) {
    u8* a = (u8*)d; while (n--) *a++ = (u8)v; return d;
}

static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

// ---------- строки ----------
static int  slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static bool seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char* d, const char* s, int max) {
    int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0;
}
static int itoa(u32 v, char* b) {
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = '0' + v % 10; v /= 10; }
    int l = 0; while (n) b[l++] = t[--n];
    b[l] = 0; return l;
}

// ---------- события ввода ----------
enum { KEY_UP = 1001, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_DEL };
struct Ev { int type; int key; bool click; bool moved; };   // type: 1 = клавиша, 2 = мышь

// ---------- драйвер VGA (текст 80x25) ----------
namespace vga {
    volatile u16* const mem = (volatile u16*)0xB8000;
    int x = 0, y = 0;
    u8 color = 0x0F;

    void hwcursor(bool on) {
        if (on) { outb(0x3D4, 0x0A); outb(0x3D5, 14); outb(0x3D4, 0x0B); outb(0x3D5, 15); }
        else    { outb(0x3D4, 0x0A); outb(0x3D5, 0x20); }
    }
    void cursor() {
        u16 p = y * 80 + x;
        outb(0x3D4, 14); outb(0x3D5, p >> 8);
        outb(0x3D4, 15); outb(0x3D5, p & 0xFF);
    }
    void clear() {
        for (int i = 0; i < 80 * 25; i++) mem[i] = (color << 8) | ' ';
        x = y = 0; cursor();
    }
    void scroll() {
        for (int i = 0; i < 80 * 24; i++) mem[i] = mem[i + 80];
        for (int i = 80 * 24; i < 80 * 25; i++) mem[i] = (color << 8) | ' ';
        y = 24;
    }
    void putc(char c) {
        if (c == '\n') { x = 0; y++; }
        else if (c == '\b') { if (x > 0) { x--; mem[y * 80 + x] = (color << 8) | ' '; } }
        else if (c == '\t') { x = (x + 4) & ~3; }
        else { mem[y * 80 + x] = (color << 8) | (u8)c; x++; }
        if (x >= 80) { x = 0; y++; }
        if (y >= 25) scroll();
        cursor();
    }
    void puts(const char* s) { while (*s) putc(*s++); }
    void dec(u32 v, int pad = 0) {
        char b[12]; int n = 0;
        if (!v) b[n++] = '0';
        while (v) { b[n++] = '0' + v % 10; v /= 10; }
        while (n < pad) b[n++] = '0';
        while (n) putc(b[--n]);
    }

    // рисование прямо в видеопамять (для рабочего стола)
    void put(int px, int py, u8 ch, u8 a) {
        if (px < 0 || px >= 80 || py < 0 || py >= 25) return;
        mem[py * 80 + px] = (a << 8) | ch;
    }
    void text(int px, int py, const char* s, u8 a) { while (*s) put(px++, py, (u8)*s++, a); }
    void fill(int px, int py, int w, int h, u8 ch, u8 a) {
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(px + i, py + j, ch, a);
    }
    void box(int px, int py, int w, int h, u8 a) {   // двойная рамка CP437
        put(px, py, 0xC9, a); put(px + w - 1, py, 0xBB, a);
        put(px, py + h - 1, 0xC8, a); put(px + w - 1, py + h - 1, 0xBC, a);
        for (int i = 1; i < w - 1; i++) { put(px + i, py, 0xCD, a); put(px + i, py + h - 1, 0xCD, a); }
        for (int j = 1; j < h - 1; j++) { put(px, py + j, 0xBA, a); put(px + w - 1, py + j, 0xBA, a); }
    }
}

// ---------- драйвер клавиатуры PS/2 (scancode set 1) ----------
namespace kbd {
    bool shift = false, ext = false;
    const char* n1 = "1234567890-=";   const char* s1 = "!@#$%^&*()_+";
    const char* nq = "qwertyuiop[]";   const char* sq = "QWERTYUIOP{}";
    const char* na = "asdfghjkl;'`";   const char* sa = "ASDFGHJKL:\"~";
    const char* nz = "zxcvbnm,./";     const char* sz = "ZXCVBNM<>?";

    int translate(u8 sc) {
        const char *n = 0, *s = 0; int i = 0;
        if      (sc >= 0x02 && sc <= 0x0D) { i = sc - 0x02; n = n1; s = s1; }
        else if (sc >= 0x10 && sc <= 0x1B) { i = sc - 0x10; n = nq; s = sq; }
        else if (sc >= 0x1E && sc <= 0x29) { i = sc - 0x1E; n = na; s = sa; }
        else if (sc >= 0x2C && sc <= 0x35) { i = sc - 0x2C; n = nz; s = sz; }
        else switch (sc) {
            case 0x01: return 27;
            case 0x0E: return '\b';
            case 0x0F: return '\t';
            case 0x1C: return '\n';
            case 0x39: return ' ';
            case 0x2B: return shift ? '|' : '\\';
            default:   return 0;
        }
        return shift ? s[i] : n[i];
    }
    bool feed(u8 sc, Ev& e) {
        if (sc == 0xE0) { ext = true; return false; }
        bool wasExt = ext; ext = false;
        if (!wasExt) {
            if (sc == 0x2A || sc == 0x36) { shift = true;  return false; }
            if (sc == 0xAA || sc == 0xB6) { shift = false; return false; }
        }
        if (sc & 0x80) return false;                  // отпускание клавиши
        int k = 0;
        if (wasExt) {
            switch (sc) {
                case 0x48: k = KEY_UP;    break;
                case 0x50: k = KEY_DOWN;  break;
                case 0x4B: k = KEY_LEFT;  break;
                case 0x4D: k = KEY_RIGHT; break;
                case 0x53: k = KEY_DEL;   break;
                case 0x1C: k = '\n';      break;
            }
        } else k = translate(sc);
        if (!k) return false;
        e.type = 1; e.key = k;
        return true;
    }
}

// ---------- драйвер мыши PS/2 (опрос, без прерываний) ----------
namespace mouse {
    int x = 40, y = 12, ax = 0, ay = 0, idx = 0;
    u8 btn = 0, pkt[3];

    void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t) {} }
    bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t) {} return t > 0; }
    void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    u8   read()      { return wait_r() ? inb(0x60) : 0; }

    void init() {
        wait_w(); outb(0x64, 0xA8);                   // включить второй порт
        wait_w(); outb(0x64, 0x20);
        u8 st = read(); st &= ~0x20;                  // включить тактирование мыши
        wait_w(); outb(0x64, 0x60);
        wait_w(); outb(0x60, st);
        write(0xF6); read();                          // настройки по умолчанию
        write(0xF4); read();                          // включить передачу данных
    }
    bool feed(u8 b, Ev& e) {
        if (idx == 0 && !(b & 8)) return false;       // потеряли синхронизацию
        pkt[idx++] = b;
        if (idx < 3) return false;
        idx = 0;
        int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
        int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
        int ox = x, oy = y;
        ax += dx; ay += dy;
        while (ax >=  4) { x++; ax -= 4; }
        while (ax <= -4) { x--; ax += 4; }
        while (ay >=  8) { y--; ay -= 8; }
        while (ay <= -8) { y++; ay += 8; }
        if (x < 0) x = 0; if (x > 79) x = 79;
        if (y < 0) y = 0; if (y > 24) y = 24;
        u8 nb = pkt[0] & 7;
        e.type = 2; e.click = (nb & 1) && !(btn & 1); e.moved = (ox != x || oy != y);
        btn = nb;
        return true;
    }
}

static bool poll(Ev& e) {
    u8 st = inb(0x64);
    if (!(st & 1)) return false;
    u8 b = inb(0x60);
    e.type = 0; e.key = 0; e.click = false; e.moved = false;
    if (st & 0x20) return mouse::feed(b, e);
    return kbd::feed(b, e);
}
static int getkey() {
    Ev e;
    for (;;) { if (poll(e) && e.type == 1) return e.key; asm volatile("pause"); }
}

// ---------- драйвер часов RTC ----------
namespace rtc {
    u8 rd(u8 r) { outb(0x70, r); return inb(0x71); }
    u8 bcd(u8 v) { return (v & 0x0F) + (v >> 4) * 10; }
    void get(u8& h, u8& m, u8& s, u8& d, u8& mo, u8& y) {
        while (rd(0x0A) & 0x80) {}
        s = rd(0); m = rd(2); h = rd(4); d = rd(7); mo = rd(8); y = rd(9);
        if (!(rd(0x0B) & 4)) { s = bcd(s); m = bcd(m); h = bcd(h & 0x7F); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    }
    void print() {
        u8 h, m, s, d, mo, y; get(h, m, s, d, mo, y);
        vga::dec(2000 + y, 4); vga::putc('-'); vga::dec(mo, 2); vga::putc('-'); vga::dec(d, 2); vga::putc(' ');
        vga::dec(h, 2); vga::putc(':'); vga::dec(m, 2); vga::putc(':'); vga::dec(s, 2); vga::putc('\n');
    }
    void hhmm(char* o) {
        u8 h, m, s, d, mo, y; get(h, m, s, d, mo, y);
        o[0] = '0' + h / 10; o[1] = '0' + h % 10; o[2] = ':'; o[3] = '0' + m / 10; o[4] = '0' + m % 10; o[5] = 0;
    }
}

// ---------- файловая система в памяти (с корзиной) ----------
struct File { bool used; bool trashed; char name[16]; char data[256]; };
static File files[32];

static File* find(const char* n) {             // только активные файлы
    for (int i = 0; i < 32; i++) if (files[i].used && !files[i].trashed && seq(files[i].name, n)) return &files[i];
    return 0;
}
static File* create(const char* n) {
    if (!n[0] || slen(n) >= 16) return 0;
    File* f = find(n);
    if (f) return f;
    for (int i = 0; i < 32; i++) if (!files[i].used) {
        files[i].used = true; files[i].trashed = false; memset(files[i].data, 0, 256);
        scopy(files[i].name, n, 16);
        return &files[i];
    }
    return 0;
}
static int countFiles(bool trashed) {
    int n = 0;
    for (int i = 0; i < 32; i++) if (files[i].used && files[i].trashed == trashed) n++;
    return n;
}
static File* nth(bool trashed, int k) {
    for (int i = 0; i < 32; i++) if (files[i].used && files[i].trashed == trashed) { if (k == 0) return &files[i]; k--; }
    return 0;
}

// ======================= РАБОЧИЙ СТОЛ =======================

// курсор мыши: инвертируем цвет клетки под ним
namespace cur {
    bool shown = false; int cx = 0, cy = 0; u16 saved = 0;
    void hide() { if (shown) { vga::mem[cy * 80 + cx] = saved; shown = false; } }
    void show() {
        cx = mouse::x; cy = mouse::y; saved = vga::mem[cy * 80 + cx];
        u8 a = (u8)(saved >> 8); a = (u8)(((a & 0x0F) << 4) | (a >> 4));
        vga::mem[cy * 80 + cx] = (a << 8) | (saved & 0xFF); shown = true;
    }
}

struct Win { int x, y, w, h; };
struct Btn { int x, y; const char* t; };
static const Win FMW = {12, 2, 60, 19};     // Менеджер файлов
static const Win TRW = {20, 4, 54, 17};     // Мусорка

static bool fmOpen = false, trOpen = false;
static int  fmSel = 0, trSel = 0, fmTop = 0, trTop = 0;
static int  active = 0;                     // 0 = нет, 1 = файлы, 2 = мусорка
static int  iconFocus = 0;

static Btn btn(const Win& w, int dx, const char* t) { Btn b = {w.x + dx, w.y + w.h - 2, t}; return b; }
static bool hitBtn(const Btn& b) { return mouse::y == b.y && mouse::x >= b.x && mouse::x < b.x + slen(b.t); }
static bool inRect(const Win& w) { return mouse::x >= w.x && mouse::x < w.x + w.w && mouse::y >= w.y && mouse::y < w.y + w.h; }

static void fixSel(bool tr, int& sel, int& top, int R) {
    int n = countFiles(tr);
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    if (top > sel) top = sel;
    if (top + R <= sel) top = sel - R + 1;
    if (top < 0) top = 0;
}

// ----- диалоги -----
static void dialogBox(const char* title, int x, int y, int w, int h) {
    vga::fill(x, y, w, h, ' ', 0x70);
    vga::box(x, y, w, h, 0x70);
    vga::fill(x + 1, y, w - 2, 1, ' ', 0x1F);
    vga::text(x + 2, y, title, 0x1F);
}
static void message(const char* title, const char* body) {
    cur::hide(); dialogBox(title, 15, 7, 50, 11);
    int row = 9, col = 0;
    for (const char* p = body; *p && row < 15; p++) {
        if (col >= 46) { row++; col = 0; }
        vga::put(17 + col, row, (u8)*p, 0x70); col++;
    }
    vga::text(17, 16, " Press any key ", 0x2F);
    cur::show();
    for (;;) {
        Ev e;
        if (poll(e)) {
            if (e.type == 1 || (e.type == 2 && e.click)) break;
            if (e.type == 2 && e.moved) { cur::hide(); cur::show(); }
        }
        asm volatile("pause");
    }
}
static bool confirm(const char* q) {
    cur::hide(); dialogBox("Confirm", 20, 9, 40, 7, 0 ? 0 : 0), (void)0;
    return false;
}
