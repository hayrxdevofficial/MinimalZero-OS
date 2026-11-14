#include "common.h"
#include "apps.h"

// ============================================================
//  iMimi OS — main kernel
// ============================================================

// Глобальные переменные
int  g_scr_w = 1024, g_scr_h = 768;
int  g_mx = 512, g_my = 384;
u8   g_mbtn = 0;
bool g_press = false, g_release = false;
int  g_px = 0, g_py = 0;
int  g_mspeed = 1;
u32  g_ticks = 0;
int  g_volume = 50;
bool g_kb_mouse = true, g_hayricon = false;
int  g_coins = 0;
bool g_teen_unlocked = false, g_cool_unlocked = false, g_auto_click = false;
int  g_auto_level = 0, g_click_power = 0;
int  g_cur_skin = 0;

// FS
Node fs[MAXN];
int fs_docs = -1, fs_pics = -1;

int fs_new(int par, const char* nm, bool dir) {
    for (int i = 1; i < MAXN; i++) if (!fs[i].used) {
        fs[i].used = true; fs[i].dir = dir; fs[i].trashed = false; fs[i].parent = par;
        scopy(fs[i].name, nm, NAMELEN); fs[i].len = 0; fs[i].data[0] = 0;
        return i;
    }
    return -1;
}
void fs_set_txt(int idx, const char* t) {
    int n = slen(t); if (n > DATALEN - 1) n = DATALEN - 1;
    for (int i = 0; i < n; i++) fs[idx].data[i] = t[i];
    fs[idx].data[n] = 0; fs[idx].len = n;
}
int fs_list(int par, int* out, int max) {
    int n = 0; for (int i = 1; i < MAXN && n < max; i++)
        if (fs[i].used && !fs[i].trashed && fs[i].parent == par) out[n++] = i;
    return n;
}
void fs_destroy(int idx) {
    fs[idx].used = false;
    for (int j = 1; j < MAXN; j++) if (fs[j].used && fs[j].parent == idx) fs_destroy(j);
}
bool fs_reachable(int idx) {
    for (int g = 0; g < MAXN; g++) {
        if (idx == 0) return true;
        if (idx < 0 || !fs[idx].used || fs[idx].trashed) return false;
        idx = fs[idx].parent;
    }
    return false;
}
void fs_path(int idx, char* out, int max) {
    int st[16], n = 0;
    while (idx > 0 && n < 16) { st[n++] = idx; idx = fs[idx].parent; }
    int p = 0; out[0] = 0;
    while (n) { p = sappend(out, p, "/", max); p = sappend(out, p, fs[st[--n]].name, max); }
}
void fs_init() {
    memset(fs, 0, sizeof(fs));
    fs[0].used = true; fs[0].dir = true;
    scopy(fs[0].name, "/", NAMELEN);
    fs_docs = fs_new(0, "Documents", true);
    fs_pics = fs_new(0, "Pictures", true);
}

// ============================================================
//  Время через TSC
// ============================================================
static u64 tsc_base = 0;
void time_init() {
    u32 lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    tsc_base = ((u64)hi << 32) | lo;
}
u32 time_ms() {
    u32 lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    u64 now = ((u64)hi << 32) | lo;
    u32 delta = (u32)(now - tsc_base);
    return delta / 3000000;
}

// ============================================================
//  Звук
// ============================================================
void beep(u32 freq, u32 ms) {
    if (g_volume == 0 || freq < 20) return;
    u32 div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, (u8)(div & 0xFF));
    outb(0x42, (u8)((div >> 8) & 0xFF));
    outb(0x61, inb(0x61) | 3);
    for (volatile u32 i = 0; i < ms * 10000; i++);
    outb(0x61, inb(0x61) & 0xFC);
}

void rtc_get(int& h, int& m, int& s) { h = 12; m = 0; s = 0; }

// ============================================================
//  Окна
// ============================================================
enum { APP_CLI, APP_FILES, APP_BROWSER, APP_INSTALLER, APP_CLICKER, APP_CALC, APP_SET, NAPPS };
static const char* app_title[NAPPS] = {
    "Terminal", "Files", "Browser", "Installer", "Mimi Clicker", "Calculator", "Settings"
};

struct Win { bool open, min; int x, y, w, h; };
static Win wins[NAPPS];
static int zord[NAPPS];
static int drag_win = -1, drag_dx = 0, drag_dy = 0;

static void init_wins() {
    static const int sizes[NAPPS][2] = {
        {600, 400}, {700, 480}, {800, 550}, {500, 350},
        {700, 500}, {340, 460}, {500, 480}
    };
    for (int i = 0; i < NAPPS; i++) {
        wins[i].open = false; wins[i].min = false;
        wins[i].w = sizes[i][0]; wins[i].h = sizes[i][1];
        if (wins[i].w > g_scr_w - 40) wins[i].w = g_scr_w - 40;
        if (wins[i].h > g_scr_h - 40) wins[i].h = g_scr_h - 40;
        wins[i].x = (g_scr_w - wins[i].w) / 2 + (i - 3) * 20;
        wins[i].y = (g_scr_h - wins[i].h) / 2 + (i - 3) * 15;
        if (wins[i].x < 0) wins[i].x = 0;
        if (wins[i].y < 0) wins[i].y = 0;
        zord[i] = i;
    }
}

static void front(int a) {
    int p = 0; for (int i = 0; i < NAPPS; i++) if (zord[i] == a) p = i;
    for (int i = p; i < NAPPS - 1; i++) zord[i] = zord[i + 1];
    zord[NAPPS - 1] = a;
}

static int top_win() {
    for (int i = NAPPS - 1; i >= 0; i--) {
        int a = zord[i]; if (wins[a].open && !wins[a].min) return a;
    }
    return -1;
}

static int win_at(int x, int y) {
    for (int i = NAPPS - 1; i >= 0; i--) {
        int a = zord[i]; Win& w = wins[a];
        if (w.open && !w.min && x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h) return a;
    }
    return -1;
}

// ============================================================
//  Иконки
// ============================================================
static void draw_app_icon(int a, int x, int y) {
    if (a == APP_CLI) {
        gfx::round(x + 4, y + 4, 48, 48, 6, 0x111111);
        text::draw(">_", x + 12, y + 18, 0x00FF00, 2);
    } else if (a == APP_FILES) {
        gfx::round(x + 4, y + 12, 48, 40, 5, 0xF5C542);
        gfx::round(x + 4, y + 12, 20, 10, 3, 0xE0A800);
    } else if (a == APP_BROWSER) {
        gfx::circle(x + 28, y + 28, 24, 0x3B82F6);
        gfx::circle(x + 28, y + 28, 18, 0x60A5FA);
        gfx::rect(x + 27, y + 6, 2, 44, 0xFFFFFF);
        gfx::rect(x + 6, y + 27, 44, 2, 0xFFFFFF);
    } else if (a == APP_INSTALLER) {
        gfx::round(x + 6, y + 6, 44, 44, 6, 0x22C55E);
        text::draw("+", x + 22, y + 18, 0xFFFFFF, 3);
    } else if (a == APP_CLICKER) {
        u32 col = (g_cur_skin == 1) ? 0xFF6FB5 : (g_cur_skin == 2) ? 0x7DE3D0 : 0xFFD700;
        gfx::circle(x + 28, y + 28, 24, col);
        gfx::circle(x + 22, y + 22, 8, 0xFFFFFF);
        gfx::circle(x + 28, y + 28, 5, 0x000000);
    } else if (a == APP_CALC) {
        gfx::round(x + 8, y + 4, 40, 48, 6, 0x2D3340);
        gfx::rect(x + 12, y + 8, 32, 10, 0x9BE7A7);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                gfx::round(x + 12 + c * 8, y + 22 + r * 7, 6, 5, 1,
                           c == 3 ? 0xF59E0B : 0xE5E7EB);
    } else if (a == APP_SET) {
        gfx::circle(x + 28, y + 28, 22, 0xAEB6C4);
        gfx::circle(x + 28, y + 28, 12, 0x4B5563);
    }
}

// ============================================================
//  Render
// ============================================================
static void render() {
    gfx::grad_v(0, 0, g_scr_w, g_scr_h, 0x1A1A2E, 0x16213E);
    gfx::circle_a(g_scr_w * 3 / 4, g_scr_h / 3, g_scr_h / 3, 0x5B7CFA, 40);
    gfx::circle_a(g_scr_w / 3, g_scr_h * 3 / 4, g_scr_h / 4, 0xFF6FB5, 34);

    static const int desk_apps[7] = {
        APP_CLI, APP_FILES, APP_BROWSER, APP_INSTALLER,
        APP_CLICKER, APP_CALC, APP_SET
    };
    for (int i = 0; i < 7; i++) {
        int ix = 20, iy = 20 + i * 80;
        if (hit_rect(ix, iy, 100, 80, g_mx, g_my))
            gfx::round_a(ix, iy, 100, 80, 10, 0xFFFFFF, 70); // ИСПРАВЛЕНО
        draw_app_icon(desk_apps[i], ix + 22, iy + 4);
        const char* t = app_title[desk_apps[i]];
        int tw = text::width(t, 1);
        text::draw(t, ix + 50 - tw / 2, iy + 62, 0xFFFFFF, 1);
    }

    for (int i = 0; i < NAPPS; i++) {
        int a = zord[i];
        if (wins[a].open && !wins[a].min) {
            Win& w = wins[a];
            bool active = (top_win() == a);

            gfx::round_a(w.x + 4, w.y + 6, w.w, w.h, 12, 0x000000, 80);

            u32 title_col = active ? 0x2D4A9A : 0x353A50;
            gfx::round(w.x, w.y, w.w, 32, 10, title_col);
            gfx::rect(w.x, w.y + 20, w.w, 12, title_col);

            text::draw(app_title[a], w.x + 16, w.y + 12, 0xFFFFFF, 1);

            int cx_btn = w.x + w.w - 28;
            if (hit_rect(cx_btn, w.y + 8, 20, 16, g_mx, g_my))
                gfx::round(cx_btn, w.y + 8, 20, 16, 4, 0xEF4444);
            text::draw_c("x", cx_btn, w.y + 8, 20, 16, 0xFFFFFF, 1);

            int mx_btn = w.x + w.w - 52;
            if (hit_rect(mx_btn, w.y + 8, 20, 16, g_mx, g_my))
                gfx::round(mx_btn, w.y + 8, 20, 16, 4, 0x6B7BB5);
            text::draw_c("-", mx_btn, w.y + 8, 20, 16, 0xFFFFFF, 1);

            gfx::rect(w.x, w.y + 32, w.w, w.h - 32, 0xFFFFFF);

            switch (a) {
                case APP_CLI:       cli_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_FILES:     files_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_BROWSER:   browser_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_INSTALLER: installer_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_CLICKER:   clicker_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_CALC:      calc_draw(w.x, w.y + 32, w.w, w.h - 32); break;
                case APP_SET:       settings_draw(w.x, w.y + 32, w.w, w.h - 32); break;
            }
        }
    }

    gfx::rect_a(0, g_scr_h - 44, g_scr_w, 44, 0x10142A, 215);
    gfx::rect(0, g_scr_h - 44, g_scr_w, 1, 0x4C5C9A);

    gfx::round(6, g_scr_h - 38, 96, 32, 8, 0x3B82F6);
    text::draw_c("iMimi", 6, g_scr_h - 38, 96, 32, 0xFFFFFF, 1);

    int slot = 0;
    for (int a = 0; a < NAPPS; a++) {
        if (!wins[a].open) continue;
        int bx = 110 + slot * 150;
        gfx::round(bx, g_scr_h - 38, 140, 32, 8,
                   top_win() == a ? 0x3D5CB8 : 0x1F2748);
        text::draw_n(app_title[a], bx + 10, g_scr_h - 26, 0xFFFFFF, 15);
        slot++;
    }

    char clock[8] = "12:00";
    text::draw(clock, g_scr_w - 70, g_scr_h - 26, 0xFFFFFF, 2);

    gfx::rect(g_mx, g_my, 3, 3, 0xFFFFFF);
    gfx::rect(g_mx - 1, g_my - 1, 5, 5, 0x000000);
}

// ============================================================
//  Ввод
// ============================================================
void on_key(int k) {
    if (k == K_F1) {
        int t = top_win();
        if (t >= 0) { wins[t].open = false; return; }
    }

    int t = top_win();
    if (t < 0) return;

    if (k == 27) { wins[t].open = false; return; }

    switch (t) {
        case APP_CLI:       cli_key(k); break;
        case APP_BROWSER:   browser_key(k); break;
        case APP_CALC:      calc_key(k); break;
        case APP_SET:       settings_key(k); break;
        case APP_FILES:     files_key(k); break;
    }

    if (g_kb_mouse) {
        int step = 8 * g_mspeed;
        if (k == K_LEFT || k == 'a') g_mx -= step;
        if (k == K_RIGHT || k == 'd') g_mx += step;
        if (k == K_UP || k == 'w') g_my -= step;
        if (k == K_DOWN || k == 's') g_my += step;
        if (g_mx < 0) g_mx = 0;
        if (g_mx >= g_scr_w) g_mx = g_scr_w - 1;
        if (g_my < 0) g_my = 0;
        if (g_my >= g_scr_h) g_my = g_scr_h - 1;
        if (k == ' ') { g_press = true; g_px = g_mx; g_py = g_my; }
    }
}

static void handle_press(int px, int py) {
    if (py >= g_scr_h - 44) {
        if (px >= 6 && px <= 102) {
            wins[APP_FILES].open = true; wins[APP_FILES].min = false; front(APP_FILES); return;
        }
        int slot = 0;
        for (int a = 0; a < NAPPS; a++) {
            if (!wins[a].open) continue;
            int bx = 110 + slot * 150;
            if (px >= bx && px <= bx + 140) {
                if (wins[a].min) { wins[a].min = false; front(a); }
                else if (top_win() == a) wins[a].min = true;
                else front(a);
                return;
            }
            slot++;
        }
        return;
    }

    int a = win_at(px, py);
    if (a >= 0) {
        front(a); Win& w = wins[a];
        if (py < w.y + 32) {
            if (px >= w.x + w.w - 28 && px <= w.x + w.w - 8) { wins[a].open = false; return; }
            if (px >= w.x + w.w - 52 && px <= w.x + w.w - 32) { wins[a].min = true; return; }
            drag_win = a; drag_dx = px - w.x; drag_dy = py - w.y; return;
        }
        switch (a) {
            case APP_CLI:       cli_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_FILES:     files_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_BROWSER:   browser_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_INSTALLER: installer_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_CLICKER:   clicker_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_CALC:      calc_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
            case APP_SET:       settings_click(px, py, w.x, w.y + 32, w.w, w.h - 32); break;
        }
        return;
    }

    static const int desk_apps[7] = {
        APP_CLI, APP_FILES, APP_BROWSER, APP_INSTALLER,
        APP_CLICKER, APP_CALC, APP_SET
    };
    for (int i = 0; i < 7; i++) {
        int ix = 20, iy = 20 + i * 80;
        if (px >= ix && px <= ix + 100 && py >= iy && py <= iy + 80) {
            wins[desk_apps[i]].open = true; wins[desk_apps[i]].min = false; front(desk_apps[i]); return;
        }
    }
}

static void handle_release() { drag_win = -1; }

// ============================================================
//  kmain
// ============================================================
extern "C" void kmain(multiboot_info* mb, u32 magic) {
    (void)magic;
    gfx::init(mb);
    g_scr_w = gfx::width();
    g_scr_h = gfx::height();

    time_init();
    mouse::init();
    net_init();
    fs_init();
    settings_load();
    init_wins();
    clicker_init();
    calc_init();

    gfx::clear(0x121212);
    text::draw_c("iMimi OS", 0, g_scr_h / 2 - 40, g_scr_w, 40, 0xFFFFFF, 3);
    text::draw_c("Preparing...", 0, g_scr_h / 2 + 10, g_scr_w, 20, 0x9CA3AF, 1);
    gfx::swap();

    for (volatile u32 i = 0; i < 50000000; i++);

    render();
    gfx::swap();

    u32 last_tick = 0;

    while (1) {
        bool need = false;

        while (inb(0x64) & 1) {
            u8 b = inb(0x60);
            if (b & 0x20) mouse::feed(b);
            else kbd::feed(b);
            need = true;
        }

        int b = net_poll_byte();
        if (b >= 0) need = true;

        if (g_press) { g_press = false; handle_press(g_px, g_py); need = true; }
        if (g_release) { g_release = false; handle_release(); need = true; }

        if (drag_win >= 0) {
            wins[drag_win].x = g_mx - drag_dx;
            wins[drag_win].y = g_my - drag_dy;
            if (wins[drag_win].x < 0) wins[drag_win].x = 0;
            if (wins[drag_win].y < 0) wins[drag_win].y = 0;
            if (wins[drag_win].x + wins[drag_win].w > g_scr_w) wins[drag_win].x = g_scr_w - wins[drag_win].w;
            if (wins[drag_win].y + wins[drag_win].h > g_scr_h - 44) wins[drag_win].y = g_scr_h - 44 - wins[drag_win].h;
            need = true;
        }

        u32 now = time_ms();
        if (now - last_tick >= 16) {
            last_tick = now;
            if (wins[APP_CLICKER].open) { clicker_tick(); need = true; }
        }

        if (need) { render(); gfx::swap(); }
        else asm volatile("pause");
    }
}