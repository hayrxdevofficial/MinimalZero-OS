// MinimalZero 0.1: ядро и драйверы на C++ (freestanding, 32 бита)
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern "C" void* memcpy(void* d, const void* s, u32 n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s; while (n--) *a++ = *b++; return d;
}
extern "C" void* memset(void* d, int v, u32 n) {
    u8* a = (u8*)d; while (n--) *a++ = (u8)v; return d;
}

// ---------- порты ----------
static inline void outb(u16 p, u8 v) { asm volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8   inb(u16 p)        { u8 v; asm volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

// ---------- строки ----------
static int  slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static bool seq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// ---------- драйвер VGA (текст 80x25) ----------
namespace vga {
    volatile u16* const mem = (volatile u16*)0xB8000;
    int x = 0, y = 0;
    u8 color = 0x0F;

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
}

// ---------- драйвер клавиатуры PS/2 (опрос, scancode set 1) ----------
namespace kbd {
    bool shift = false, ext = false;
    const char* n1 = "1234567890-=";   const char* s1 = "!@#$%^&*()_+";
    const char* nq = "qwertyuiop[]";   const char* sq = "QWERTYUIOP{}";
    const char* na = "asdfghjkl;'`";   const char* sa = "ASDFGHJKL:\"~";
    const char* nz = "zxcvbnm,./";     const char* sz = "ZXCVBNM<>?";

    char translate(u8 sc) {
        const char *n = 0, *s = 0; int i = 0;
        if      (sc >= 0x02 && sc <= 0x0D) { i = sc - 0x02; n = n1; s = s1; }
        else if (sc >= 0x10 && sc <= 0x1B) { i = sc - 0x10; n = nq; s = sq; }
        else if (sc >= 0x1E && sc <= 0x29) { i = sc - 0x1E; n = na; s = sa; }
        else if (sc >= 0x2C && sc <= 0x35) { i = sc - 0x2C; n = nz; s = sz; }
        else switch (sc) {
            case 0x0E: return '\b';
            case 0x0F: return '\t';
            case 0x1C: return '\n';
            case 0x39: return ' ';
            case 0x2B: return shift ? '|' : '\\';
            default:   return 0;
        }
        return shift ? s[i] : n[i];
    }
    char getc() {
        for (;;) {
            u8 st = inb(0x64);
            if (!(st & 1)) { asm volatile("pause"); continue; }
            u8 sc = inb(0x60);
            if (st & 0x20) continue;                 // байт от мыши, пока пропускаем
            if (sc == 0xE0) { ext = true; continue; }
            if (ext) { ext = false; continue; }      // стрелки и т.п. пока не используем
            if (sc == 0x2A || sc == 0x36) { shift = true;  continue; }
            if (sc == 0xAA || sc == 0xB6) { shift = false; continue; }
            if (sc & 0x80) continue;                 // отпускание клавиши
            char c = translate(sc);
            if (c) return c;
        }
    }
}

// ---------- драйвер часов реального времени (CMOS RTC) ----------
namespace rtc {
    u8 rd(u8 r) { outb(0x70, r); return inb(0x71); }
    u8 bcd(u8 v) { return (v & 0x0F) + (v >> 4) * 10; }
    void print() {
        while (rd(0x0A) & 0x80) {}
        u8 s = rd(0), m = rd(2), h = rd(4), d = rd(7), mo = rd(8), y = rd(9);
        if (!(rd(0x0B) & 4)) { s = bcd(s); m = bcd(m); h = bcd(h & 0x7F); d = bcd(d); mo = bcd(mo); y = bcd(y); }
        vga::dec(2000 + y, 4); vga::putc('-'); vga::dec(mo, 2); vga::putc('-'); vga::dec(d, 2); vga::putc(' ');
        vga::dec(h, 2); vga::putc(':'); vga::dec(m, 2); vga::putc(':'); vga::dec(s, 2); vga::putc('\n');
    }
}

// ---------- файловая система в памяти ----------
struct File { bool used; char name[16]; char data[256]; };
static File files[16];

static File* find(const char* n) {
    for (int i = 0; i < 16; i++) if (files[i].used && seq(files[i].name, n)) return &files[i];
    return 0;
}
static File* create(const char* n) {
    if (slen(n) >= 16) return 0;
    File* f = find(n);
    if (f) return f;
    for (int i = 0; i < 16; i++) if (!files[i].used) {
        files[i].used = true; memset(files[i].data, 0, 256);
        int k = 0; while (n[k]) { files[i].name[k] = n[k]; k++; } files[i].name[k] = 0;
        return &files[i];
    }
    return 0;
}

// ---------- оболочка ----------
static u32 mem_mb = 0;

static void banner() {
    vga::color = 0x1F; vga::clear();
    vga::puts(" MinimalZero 0.1                                                                \n");
    vga::color = 0x0B;
    vga::puts("\n  __  __ _       _                 _ ______\n"
              " |  \\/  (_)     (_)               | |___  /\n"
              " | \\  / |_ _ __  _ _ __ ___   __ _| |  / /  ___ _ __ ___\n"
              " | |\\/| | | '_ \\| | '_ ` _ \\ / _` | | / /  / _ \\ '__/ _ \\\n"
              " | |  | | | | | | | | | | | | (_| | |/ /__|  __/ | | (_) |\n"
              " |_|  |_|_|_| |_|_|_| |_| |_|\\__,_|_/_____/\\___|_|  \\___/\n\n");
    vga::color = 0x0F;
    vga::puts("  Введите help (ввод латиницей)\n\n");
}

static void readline(char* buf, int max) {
    int n = 0;
    for (;;) {
        char c = kbd::getc();
        if (c == '\n') { vga::putc('\n'); buf[n] = 0; return; }
        if (c == '\b') { if (n) { n--; vga::putc('\b'); } continue; }
        if (c >= 32 && n < max - 1) { buf[n++] = c; vga::putc(c); }
    }
}
static int split(char* l, char** av, int max) {
    int n = 0;
    while (*l && n < max) {
        while (*l == ' ') *l++ = 0;
        if (!*l) break;
        av[n++] = l;
        while (*l && *l != ' ') l++;
    }
    return n;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void run(int ac, char** av) {
    if (!ac) return;
    const char* c = av[0];
    if (seq(c, "help")) {
        vga::puts("help clear ver mem date ls cat <f> write <f> <text> rm <f>\n"
                  "echo <text> color <bgfg hex, напр. 1f> reboot halt\n");
    } else if (seq(c, "clear")) { vga::clear(); }
    else if (seq(c, "ver"))     { vga::puts("MinimalZero 0.1 (C++ kernel)\n"); }
    else if (seq(c, "mem"))     { vga::puts("RAM: "); vga::dec(mem_mb); vga::puts(" MB\n"); }
    else if (seq(c, "date"))    { rtc::print(); }
    else if (seq(c, "echo"))    { for (int i = 1; i < ac; i++) { vga::puts(av[i]); vga::putc(' '); } vga::putc('\n'); }
    else if (seq(c, "ls")) {
        int n = 0;
        for (int i = 0; i < 16; i++) if (files[i].used) { vga::puts(files[i].name); vga::putc('\n'); n++; }
        if (!n) vga::puts("(пусто)\n");
    } else if (seq(c, "cat")) {
        File* f = ac > 1 ? find(av[1]) : 0;
        if (f) { vga::puts(f->data); vga::putc('\n'); } else vga::puts("нет файла\n");
    } else if (seq(c, "write")) {
        File* f = ac > 1 ? create(av[1]) : 0;
        if (!f) { vga::puts("ошибка\n"); return; }
        int k = 0;
        for (int i = 2; i < ac; i++) {
            const char* w = av[i];
            while (*w && k < 254) f->data[k++] = *w++;
            if (i + 1 < ac && k < 254) f->data[k++] = ' ';
        }
        f->data[k] = 0;
    } else if (seq(c, "rm")) {
        File* f = ac > 1 ? find(av[1]) : 0;
        if (f) f->used = false; else vga::puts("нет файла\n");
    } else if (seq(c, "color")) {
        int b = ac > 1 && av[1][0] ? hexv(av[1][0]) : -1, f = ac > 1 && av[1][0] && av[1][1] ? hexv(av[1][1]) : -1;
        if (b < 0 || f < 0) vga::puts("пример: color 1f\n");
        else { vga::color = (b << 4) | f; vga::clear(); }
    } else if (seq(c, "reboot")) {
        while (inb(0x64) & 2) {}
        outb(0x64, 0xFE);
    } else if (seq(c, "halt")) {
        vga::puts("Можно выключать.\n"); asm volatile("cli"); for (;;) asm volatile("hlt");
    } else { vga::puts("неизвестная команда: "); vga::puts(c); vga::putc('\n'); }
}

struct MultibootInfo { u32 flags, mem_lower, mem_upper; };

extern "C" void kmain(u32 magic, MultibootInfo* mbi) {
    if (magic == 0x2BADB002 && (mbi->flags & 1)) mem_mb = mbi->mem_upper / 1024 + 1;
    create("readme.txt");
    File* f = find("readme.txt");
    const char* t = "Hello from MinimalZero!";
    int k = 0; while (t[k]) { f->data[k] = t[k]; k++; }

    banner();
    char line[128]; char* av[8];
    for (;;) {
        vga::color = 0x0A; vga::puts("mz> "); vga::color = 0x0F;
        readline(line, 128);
        run(split(line, av, 8), av);
    }
}
