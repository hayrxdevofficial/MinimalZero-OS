#include "common.h"
#include "apps.h"

// ============================================================
//  Settings — все переключатели и сохранение в CMOS
// ============================================================

// ---- CMOS реализация (нужна для сохранения) ----
u8 cmos_read(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}

void cmos_write(u8 reg, u8 val) {
    outb(0x70, (reg | 0x80));
    outb(0x71, val);
    outb(0x70, 0);
}

// ---- Сохранение всех настроек в CMOS ----
void settings_save() {
    u8 flags = 0;
    if (g_kb_mouse)      flags |= 0x01;
    if (g_hayricon)      flags |= 0x02;
    if (g_teen_unlocked) flags |= 0x04;
    if (g_cool_unlocked) flags |= 0x08;
    if (g_auto_click)    flags |= 0x10;
    cmos_write(0x40, flags);

    cmos_write(0x41, (u8)(g_coins >> 24));
    cmos_write(0x42, (u8)(g_coins >> 16));
    cmos_write(0x43, (u8)(g_coins >> 8));
    cmos_write(0x44, (u8)(g_coins));

    cmos_write(0x45, (u8)g_auto_level);
    cmos_write(0x46, (u8)g_click_power);
    cmos_write(0x47, (u8)g_cur_skin);
    cmos_write(0x48, (u8)g_volume);
    cmos_write(0x49, (u8)g_mspeed);

    cmos_write(0x4A, 0xA5);  // магический байт
}

void settings_load() {
    if (cmos_read(0x4A) != 0xA5) {
        // Первый запуск — дефолты
        g_kb_mouse = true;
        g_hayricon = false;
        g_teen_unlocked = false;
        g_cool_unlocked = false;
        g_auto_click = false;
        g_coins = 0;
        g_auto_level = 0;
        g_click_power = 0;
        g_cur_skin = 0;
        g_volume = 50;
        g_mspeed = 1;
        return;
    }

    u8 flags = cmos_read(0x40);
    g_kb_mouse      = (flags & 0x01) != 0;
    g_hayricon      = (flags & 0x02) != 0;
    g_teen_unlocked = (flags & 0x04) != 0;
    g_cool_unlocked = (flags & 0x08) != 0;
    g_auto_click    = (flags & 0x10) != 0;

    g_coins = (cmos_read(0x41) << 24) | (cmos_read(0x42) << 16) |
              (cmos_read(0x43) << 8)  | cmos_read(0x44);

    g_auto_level  = cmos_read(0x45);
    g_click_power = cmos_read(0x46);
    g_cur_skin    = cmos_read(0x47);
    g_volume      = cmos_read(0x48);
    g_mspeed      = cmos_read(0x49);

    if (g_coins < 0) g_coins = 0;
    if (g_auto_level > 5) g_auto_level = 5;
    if (g_click_power > 5) g_click_power = 5;
    if (g_cur_skin > 2) g_cur_skin = 0;
    if (g_volume > 100) g_volume = 50;
    if (g_mspeed < 1 || g_mspeed > 4) g_mspeed = 1;
}

// ---- UI ----
struct SetRow {
    const char* title;
    const char* desc;
    int type;
    int id;
};

static const SetRow rows[] = {
    {"Keyboard Mouse", "Arrows/WASD move cursor, Space clicks", 0, 0},
    {"HayrICons",      "Alternative icon on taskbar",           0, 1},
    {"Sound",          "System volume",                         1, 2},
    {"Mouse Speed",    "1=slow, 4=fast",                        1, 3},
    {"Skins unlocked", "Teen and Cool available in Clicker",    2, 4},
};
#define SET_ROWS 5

static int row_h() { return 60; }
static int row_y(int cy, int i) { return cy + 16 + i * (row_h() + 6); }

void settings_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, ch, 0xF4F6FA);

    gfx::rect(cx, cy, cw, 40, 0x1F2937);
    text::draw("Settings", cx + 16, cy + 12, 0xFFFFFF, 2);

    for (int i = 0; i < SET_ROWS; i++) {
        int ry = row_y(cy, i);
        if (ry + row_h() > cy + ch - 10) break;

        gfx::round(cx + 12, ry, cw - 24, row_h(), 8, 0xFFFFFF);

        text::draw(rows[i].title, cx + 24, ry + 10, 0x1F2937, 1);
        text::draw(rows[i].desc, cx + 24, ry + 32, 0x6B7280, 1);

        if (rows[i].type == 0) {
            bool val = false;
            if (rows[i].id == 0) val = g_kb_mouse;
            if (rows[i].id == 1) val = g_hayricon;

            int tx = cx + cw - 88;
            int ty = ry + row_h() / 2 - 13;
            gfx::round(tx, ty, 52, 26, 13, val ? 0x22C55E : 0xB8BFCC);
            gfx::circle(val ? tx + 39 : tx + 13, ty + 13, 10, 0xFFFFFF);
        } else if (rows[i].type == 1) {
            int tx = cx + cw - 140;
            int ty = ry + row_h() / 2 - 16;

            gfx::round(tx, ty, 30, 32, 6, 0x3B82F6);
            text::draw_c("-", tx, ty, 30, 32, 0xFFFFFF, 2);

            char buf[8];
            itoa_(rows[i].id == 2 ? g_volume : g_mspeed, buf);
            text::draw_c(buf, tx + 36, ty, 40, 32, 0x1F2937, 1);

            gfx::round(tx + 80, ty, 30, 32, 6, 0x3B82F6);
            text::draw_c("+", tx + 80, ty, 30, 32, 0xFFFFFF, 2);
        } else {
            char buf[32];
            if (g_teen_unlocked && g_cool_unlocked) scopy(buf, "all unlocked", 32);
            else if (g_teen_unlocked) scopy(buf, "teen unlocked", 32);
            else if (g_cool_unlocked) scopy(buf, "cool unlocked", 32);
            else scopy(buf, "locked", 32);
            int tw = text::width(buf, 1);
            text::draw(buf, cx + cw - 24 - tw, ry + row_h() / 2 - 4, 0x6B7280, 1);
        }
    }

    text::draw("Settings saved automatically", cx + 16, cy + ch - 24, 0x9CA3AF, 1);
}

void settings_click(int px, int py, int cx, int cy, int cw, int ch) {
    for (int i = 0; i < SET_ROWS; i++) {
        int ry = row_y(cy, i);
        if (ry + row_h() > cy + ch - 10) break;

        if (rows[i].type == 0 && hit_rect(cx + cw - 100, ry + 5, 80, row_h() - 10, px, py)) {
            if (rows[i].id == 0) g_kb_mouse = !g_kb_mouse;
            if (rows[i].id == 1) g_hayricon = !g_hayricon;
            beep(1000, 20);
            settings_save();
            return;
        }

        if (rows[i].type == 1) {
            int tx = cx + cw - 140;
            int ty = ry + row_h() / 2 - 16;
            if (hit_rect(tx, ty, 30, 32, px, py)) {
                if (rows[i].id == 2 && g_volume >= 10) g_volume -= 10;
                if (rows[i].id == 3 && g_mspeed > 1) g_mspeed--;
                beep(800, 20);
                settings_save();
                return;
            }
            if (hit_rect(tx + 80, ty, 30, 32, px, py)) {
                if (rows[i].id == 2 && g_volume <= 90) g_volume += 10;
                if (rows[i].id == 3 && g_mspeed < 4) g_mspeed++;
                beep(800, 20);
                settings_save();
                return;
            }
        }
    }
}

void settings_key(int k) {
    if (k == 27) return;
}