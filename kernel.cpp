// MinimalZero 0.2 — полная рабочая версия
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
[[maybe_unused]] static int itoa(u32 v, char* b) {
    char t[12]; int n = 0;
    if (!v) t[n++] = '0';
    while (v) { t[n++] = '0' + v % 10; v /= 10; }
    int l = 0; while (n) b[l++] = t[--n];
    b[l] = 0; return l;
}

// ---------- события ----------
enum { KEY_UP = 1001, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_DEL };
struct Ev { int type; int key; bool click; bool moved; };

// ---------- VGA ----------
namespace vga {
    volatile u16* const mem = (volatile u16*)0xB8000;
    int x = 0, y = 0; u8 color = 0x0F;

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
    void put(int px, int py, u8 ch, u8 a) {
        if (px < 0 || px >= 80 || py < 0 || py >= 25) return;
        mem[py * 80 + px] = (a << 8) | ch;
    }
    void text(int px, int py, const char* s, u8 a) { while (*s) put(px++, py, (u8)*s++, a); }
    void fill(int px, int py, int w, int h, u8 ch, u8 a) {
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(px + i, py + j, ch, a);
    }
    void box(int px, int py, int w, int h, u8 a) {
        put(px, py, 0xC9, a); put(px + w - 1, py, 0xBB, a);
        put(px, py + h - 1, 0xC8, a); put(px + w - 1, py + h - 1, 0xBC, a);
        for (int i = 1; i < w - 1; i++) { put(px + i, py, 0xCD, a); put(px + i, py + h - 1, 0xCD, a); }
        for (int j = 1; j < h - 1; j++) { put(px, py + j, 0xBA, a); put(px + w - 1, py + j, 0xBA, a); }
    }
}

// ---------- клавиатура ----------
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
        if (sc & 0x80) return false;
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

// ---------- мышь ----------
namespace mouse {
    int x = 40, y = 12, ax = 0, ay = 0, idx = 0;
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
    bool feed(u8 b, Ev& e) {
        if (idx == 0 && !(b & 8)) return false;
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
        if (x < 0) x = 0;
        if (x > 79) x = 79;
        if (y < 0) y = 0;
        if (y > 24) y = 24;
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
[[maybe_unused]] static int getkey() {
    Ev e;
    for (;;) { if (poll(e) && e.type == 1) return e.key; asm volatile("pause"); }
}

// ---------- RTC ----------
namespace rtc {
    u8 rd(u8 r) { outb(0x70, r); return inb(0x71); }
    u8 bcd(u8 v) { return (v & 0x0F) + (v >> 4) * 10; }
    void get(u8& h, u8& m, u8& s, u8& d, u8& mo, u8& y) {
        while (rd(0x0A) & 0x80) {}
        s = rd(0); m = rd(2); h = rd(4); d = rd(7); mo = rd(8); y = rd(9);
        if (!(rd(0x0B) & 4)) { s = bcd(s); m = bcd(m); h = bcd(h & 0x7F); d = bcd(d); mo = bcd(mo); y = bcd(y); }
    }
    void hhmm(char* o) {
        u8 h, m, s, d, mo, y; get(h, m, s, d, mo, y);
        o[0] = '0' + h / 10; o[1] = '0' + h % 10; o[2] = ':';
        o[3] = '0' + m / 10; o[4] = '0' + m % 10; o[5] = 0;
    }
}

// ---------- ФС ----------
struct File { bool used; bool trashed; char name[16]; char data[256]; };
static File files[32];

static File* find(const char* n) {
    for (int i = 0; i < 32; i++) if (files[i].used && !files[i].trashed && seq(files[i].name, n)) return &files[i];
    return 0;
}
static File* create(const char* n) {
    if (!n[0] || slen(n) >= 16) return 0;
    File* f = find(n); if (f) return f;
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

// ---------- курсор ----------
namespace cur {
    bool shown = false; int cx = 0, cy = 0; u16 saved = 0;
    void hide() { if (shown) { vga::mem[cy * 80 + cx] = saved; shown = false; } }
    void show() {
        if (shown) return;
        cx = mouse::x; cy = mouse::y; saved = vga::mem[cy * 80 + cx];
        u8 a = (u8)(saved >> 8); a = (u8)(((a & 0x0F) << 4) | (a >> 4));
        vga::mem[cy * 80 + cx] = (a << 8) | (saved & 0xFF); shown = true;
    }
}

// ---------- рабочий стол ----------
struct Win { int x, y, w, h; };
struct Btn { int x, y; const char* t; };
static const Win FMW = {12, 2, 60, 19};
static const Win TRW = {20, 4, 54, 17};

static bool fmOpen = false, trOpen = false;
static int  fmSel = 0, trSel = 0, fmTop = 0, trTop = 0;
static int  active = 0;      // 0/1/2
static int  iconFocus = 0;   // 0=Files, 1=Trash

static Btn  btn(const Win& w, int dx, const char* t) { Btn b = {w.x + dx, w.y + w.h - 2, t}; return b; }
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

// ---------- диалоги ----------
static void dialogBox(const char* title, int x, int y, int w, int h) {
    vga::fill(x, y, w, h, ' ', 0x70);
    vga::box(x, y, w, h, 0x70);
    vga::fill(x + 1, y, w - 2, 1, ' ', 0x1F);
    vga::text(x + 2, y, title, 0x1F);
}
static void message(const char* title, const char* body) {
    cur::hide(); dialogBox(title, 10, 7, 60, 11);
    int row = 9, col = 0;
    for (const char* p = body; *p && row < 15; p++) {
        if (col >= 56) { row++; col = 0; }
        vga::put(12 + col, row, (u8)*p, 0x70); col++;
    }
    vga::text(12, 16, " Press any key or click ", 0x2F);
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
    cur::hide();
    dialogBox(q, 20, 9, 40, 7);
    vga::text(22, 12, "  [ Y ]  Yes    [ N ]  No  ", 0x70);
    cur::show();
    for (;;) {
        Ev e;
        if (poll(e)) {
            if (e.type == 1) {
                if (e.key == 'y' || e.key == 'Y' || e.key == '\n') return true;
                if (e.key == 'n' || e.key == 'N' || e.key == 27) return false;
            } else if (e.type == 2) {
                if (e.moved) { cur::hide(); cur::show(); }
                if (e.click && mouse::y == 12) {
                    if (mouse::x >= 22 && mouse::x <= 33) return true;
                    if (mouse::x >= 36 && mouse::x <= 46) return false;
                }
            }
        }
        asm volatile("pause");
    }
}

// ---------- отрисовка ----------
static void drawIcon(int x, int y, char symbol, const char* name, bool focused) {
    vga::box(x, y, 6, 3, 0x70);
    vga::put(x + 2, y + 1, (u8)symbol, 0x70);
    u8 a = focused ? 0x70 : 0x1F;
    if (focused) vga::fill(x, y + 3, 6, 1, ' ', 0x70);
    vga::text(x + (6 - slen(name)) / 2, y + 3, name, a);
}

static void drawFileManager() {
    vga::fill(FMW.x, FMW.y, FMW.w, FMW.h, ' ', 0x70);
    vga::box(FMW.x, FMW.y, FMW.w, FMW.h, 0x70);
    vga::fill(FMW.x + 1, FMW.y, FMW.w - 2, 1, ' ', active == 1 ? 0x4F : 0x1F);
    vga::text(FMW.x + 2, FMW.y, "Files", active == 1 ? 0x4F : 0x1F);
    int n = countFiles(false);
    for (int i = 0; i < 15; i++) {
        int idx = fmTop + i;
        u8 a = (idx == fmSel && active == 1) ? 0x4F : 0x70;
        vga::fill(FMW.x + 1, FMW.y + 2 + i, FMW.w - 2, 1, ' ', a);
        if (idx < n) { File* f = nth(false, idx); if (f) vga::text(FMW.x + 2, FMW.y + 2 + i, f->name, a); }
    }
    int by = FMW.y + FMW.h - 2;
    vga::text(FMW.x + 2,  by, "[New]",   0x70);
    vga::text(FMW.x + 10, by, "[Del]",   0x70);
    vga::text(FMW.x + 20, by, "[Close]", 0x70);
}

static void drawTrash() {
    vga::fill(TRW.x, TRW.y, TRW.w, TRW.h, ' ', 0x70);
    vga::box(TRW.x, TRW.y, TRW.w, TRW.h, 0x70);
    vga::fill(TRW.x + 1, TRW.y, TRW.w - 2, 1, ' ', active == 2 ? 0x4F : 0x1F);
    vga::text(TRW.x + 2, TRW.y, "Trash", active == 2 ? 0x4F : 0x1F);
    int n = countFiles(true);
    for (int i = 0; i < 13; i++) {
        int idx = trTop + i;
        u8 a = (idx == trSel && active == 2) ? 0x4F : 0x70;
        vga::fill(TRW.x + 1, TRW.y + 2 + i, TRW.w - 2, 1, ' ', a);
        if (idx < n) { File* f = nth(true, idx); if (f) vga::text(TRW.x + 2, TRW.y + 2 + i, f->name, a); }
    }
    int by = TRW.y + TRW.h - 2;
    vga::text(TRW.x + 2,  by, "[Restore]", 0x70);
    vga::text(TRW.x + 12, by, "[Empty]",   0x70);
    vga::text(TRW.x + 22, by, "[Close]",   0x70);
}

static void drawDesktop() {
    vga::fill(0, 0, 80, 25, ' ', 0x1F);

    // верхняя панель
    vga::fill(0, 0, 80, 1, ' ', 0x70);
    vga::text(2, 0, "MinimalZero OS 0.2", 0x70);
    u8 h, m, s, d, mo, y;
    rtc::get(h, m, s, d, mo, y);
    char tbuf[20]; int p = 0;
    tbuf[p++] = '0' + y / 10;   tbuf[p++] = '0' + y % 10;  tbuf[p++] = '-';
    tbuf[p++] = '0' + mo / 10;  tbuf[p++] = '0' + mo % 10; tbuf[p++] = '-';
    tbuf[p++] = '0' + d / 10;   tbuf[p++] = '0' + d % 10;  tbuf[p++] = ' ';
    tbuf[p++] = '0' + h / 10;   tbuf[p++] = '0' + h % 10;  tbuf[p++] = ':';
    tbuf[p++] = '0' + m / 10;   tbuf[p++] = '0' + m % 10;  tbuf[p] = 0;
    vga::text(80 - p - 1, 0, tbuf, 0x70);

    // иконки
    drawIcon(4, 3, 'F', "Files", iconFocus == 0 && !fmOpen && !trOpen);
    drawIcon(4, 9, 'T', "Trash", iconFocus == 1 && !fmOpen && !trOpen);

    // окна
    if (fmOpen) drawFileManager();
    if (trOpen) drawTrash();

    // нижняя подсказка
    vga::fill(0, 24, 80, 1, ' ', 0x70);
    vga::text(2, 24, "Tab: switch  Enter: open  Del: trash  Esc: close  ?: help", 0x70);
}

static void redraw() { cur::hide(); drawDesktop(); cur::show(); }

// ---------- события ----------
static void onKey(int k) {
    if (k == 27) {                             // Esc
        if (active == 1) { fmOpen = false; active = 0; }
        else if (active == 2) { trOpen = false; active = 0; }
        else iconFocus = 0;
    } else if (k == '\t') {
        if (fmOpen && trOpen)  active = (active == 1) ? 2 : 1;
        else if (fmOpen)       active = 1;
        else if (trOpen)       active = 2;
        else                   iconFocus = (iconFocus + 1) % 2;
    } else if (k == '\n') {
        if (!fmOpen && !trOpen) {
            if (iconFocus == 0) { fmOpen = true; active = 1; }
            else                { trOpen = true; active = 2; }
        } else if (active == 1) {
            File* f = nth(false, fmSel);
            if (f) message("File viewer", f->data);
        } else if (active == 2) {
            File* f = nth(true, trSel);
            if (f) { f->trashed = false; fixSel(true, trSel, trTop, 13); }
        }
    } else if (k == KEY_UP) {
        if (active == 1) { fmSel--; fixSel(false, fmSel, fmTop, 15); }
        else if (active == 2) { trSel--; fixSel(true, trSel, trTop, 13); }
    } else if (k == KEY_DOWN) {
        if (active == 1) { fmSel++; fixSel(false, fmSel, fmTop, 15); }
        else if (active == 2) { trSel++; fixSel(true, trSel, trTop, 13); }
    } else if (k == KEY_DEL) {
        if (active == 1) {
            File* f = nth(false, fmSel);
            if (f) { f->trashed = true; fixSel(false, fmSel, fmTop, 15); }
        } else if (active == 2) {
            File* f = nth(true, trSel);
            if (f) { f->used = false; fixSel(true, trSel, trTop, 13); }
        }
    } else if (k == '?') {
        message("Help", "Icons: Tab + Enter. In windows: arrows navigate, Enter opens, Del trashes. Esc closes.");
    }
}

static void onMouse(const Ev& e) {
    if (!e.click) return;

    // иконки
    if (mouse::x >= 3 && mouse::x < 11) {
        if (mouse::y >= 3 && mouse::y < 7)  { fmOpen = true; active = 1; iconFocus = 0; return; }
        if (mouse::y >= 9 && mouse::y < 13) { trOpen = true; active = 2; iconFocus = 1; return; }
    }

    if (fmOpen && inRect(FMW)) {
        active = 1;
        int by = FMW.y + FMW.h - 2;
        if (mouse::y == by) {
            Btn bNew = btn(FMW, 2, "[New]");
            Btn bDel = btn(FMW, 10, "[Del]");
            Btn bCls = btn(FMW, 20, "[Close]");
            if (hitBtn(bNew)) { create("new.txt"); fixSel(false, fmSel, fmTop, 15); }
            else if (hitBtn(bDel)) {
                File* f = nth(false, fmSel);
                if (f) { f->trashed = true; fixSel(false, fmSel, fmTop, 15); }
            }
            else if (hitBtn(bCls)) { fmOpen = false; active = 0; }
            return;
        }
        if (mouse::y >= FMW.y + 2 && mouse::y < FMW.y + 2 + 15) {
            int idx = fmTop + (mouse::y - FMW.y - 2);
            if (idx < countFiles(false)) { fmSel = idx; fixSel(false, fmSel, fmTop, 15); }
        }
        return;
    }

    if (trOpen && inRect(TRW)) {
        active = 2;
        int by = TRW.y + TRW.h - 2;
        if (mouse::y == by) {
            Btn bRes = btn(TRW, 2, "[Restore]");
            Btn bEmp = btn(TRW, 12, "[Empty]");
            Btn bCls = btn(TRW, 22, "[Close]");
            if (hitBtn(bRes)) {
                File* f = nth(true, trSel);
                if (f) { f->trashed = false; fixSel(true, trSel, trTop, 13); }
            }
            else if (hitBtn(bEmp)) {
                if (confirm("Empty trash?")) {
                    for (int i = 0; i < 32; i++) if (files[i].used && files[i].trashed) files[i].used = false;
                    trSel = 0; trTop = 0;
                }
            }
            else if (hitBtn(bCls)) { trOpen = false; active = 0; }
            return;
        }
        if (mouse::y >= TRW.y + 2 && mouse::y < TRW.y + 2 + 13) {
            int idx = trTop + (mouse::y - TRW.y - 2);
            if (idx < countFiles(true)) { trSel = idx; fixSel(true, trSel, trTop, 13); }
        }
    }
}

// ---------- точка входа ----------
extern "C" void kmain() {
    vga::clear();
    vga::hwcursor(false);
    mouse::init();

    // демо-файлы
    File* f = create("readme.txt"); if (f) scopy(f->data, "Welcome to MinimalZero OS!", 256);
    f = create("hello.txt");        if (f) scopy(f->data, "Hello, World!", 256);
    f = create("notes.txt");        if (f) scopy(f->data, "Tab switches, Enter opens.", 256);

    redraw();
    for (;;) {
        Ev e;
        if (poll(e)) {
            if (e.type == 1) { onKey(e.key); redraw(); }
            else if (e.type == 2) {
                if (e.click) { onMouse(e); redraw(); }
                else if (e.moved) { cur::hide(); cur::show(); }
            }
        }
        asm volatile("pause");
    }
}
