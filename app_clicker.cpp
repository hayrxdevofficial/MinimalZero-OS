#include "common.h"
#include "apps.h"

// ============================================================
//  Mimi Clicker — полноценное приложение с магазином и сохранением
// ============================================================

// Константы магазина
#define SKIN_COST_TEEN  10
#define SKIN_COST_COOL  25
#define AUTO_CLICK_COST 15

// Анимация
static int   squish_amount = 0;       // 0..20 пикселей
static bool  squish_reversing = false;
static u32   last_squish_tick = 0;

// Авто-клик
static u32   auto_click_last = 0;

// Стоимость скорости
static int speed_cost() { return 20 + g_auto_level * 10; }
// Стоимость мощности
static int power_cost() { return 30 + g_click_power * 30; }
// Доход с клика
static int click_gain() { return 1 + g_click_power; }

void clicker_init() {
    squish_amount = 0;
    squish_reversing = false;
    last_squish_tick = time_ms();
    auto_click_last = time_ms();
}

// Цвета скинов
static u32 skin_color(int skin) {
    if (skin == 1) return 0xFF6FB5;  // Teen (розовый)
    if (skin == 2) return 0x7DE3D0;  // Cool (бирюзовый)
    return 0xFFD700;                  // Base (золотой)
}

// Рисуем Mimi
static void draw_mimi(int cx, int cy, int base_r, int skin, int squish) {
    int r = base_r - squish;
    if (r < 10) r = 10;
    u32 col = skin_color(skin);
    u32 dark = gfx::lerp(col, 0x000000, 1, 3);
    u32 light = gfx::lerp(col, 0xFFFFFF, 1, 3);

    // Тень
    gfx::circle_a(cx + 4, cy + 4, r, 0x000000, 80);
    // Основа
    gfx::circle(cx, cy, r, col);
    // Блик сверху
    gfx::circle(cx - r / 3, cy - r / 3, r / 3, light);
    // Обводка
    gfx::ring(cx, cy, r, r - 2, dark);

    // Глаза
    int eye_dx = r / 2;
    int eye_y = cy - r / 6;
    gfx::circle(cx - eye_dx, eye_y, r / 6, 0x000000);
    gfx::circle(cx + eye_dx, eye_y, r / 6, 0x000000);
    // Блики в глазах
    gfx::circle(cx - eye_dx + 2, eye_y - 2, 2, 0xFFFFFF);
    gfx::circle(cx + eye_dx + 2, eye_y - 2, 2, 0xFFFFFF);

    // Улыбка
    int smile_y = cy + r / 3;
    for (int dx = -r / 3; dx <= r / 3; dx++) {
        int dy = (dx * dx) / (r * 2);
        gfx::rect(cx + dx, smile_y + dy, 1, 1, 0x000000);
    }

    // Румянец
    if (skin == 1) {
        gfx::circle_a(cx - r / 2, cy + r / 6, r / 6, 0xFF3366, 100);
        gfx::circle_a(cx + r / 2, cy + r / 6, r / 6, 0xFF3366, 100);
    }
}

void clicker_tick() {
    u32 now = time_ms();

    // Анимация сжатия: 200 мс вниз, 200 мс вверх
    if (squish_reversing) {
        if (now - last_squish_tick > 200) {
            squish_amount -= 2;
            last_squish_tick = now;
            if (squish_amount <= 0) {
                squish_amount = 0;
                squish_reversing = false;
            }
        }
    }

    // Авто-клик
    if (g_auto_click) {
        u32 interval = 1000 - g_auto_level * 100;
        if (interval < 100) interval = 100;
        if (now - auto_click_last >= interval) {
            auto_click_last = now;
            g_coins += click_gain();
        }
    }
}

void clicker_draw(int cx, int cy, int cw, int ch) {
    // Фон с градиентом
    gfx::grad_v(cx, cy, cw, ch, 0xFFFFFF, 0xDCE7FB);

    // Счётчик монет
    gfx::round(cx + 16, cy + 12, 200, 44, 22, 0xFFF7DC);
    gfx::circle(cx + 36, cy + 34, 14, 0xFFD700);
    gfx::circle(cx + 36, cy + 34, 10, 0xFFE04A);
    text::draw_int(g_coins, cx + 60, cy + 22, 0xB77900, 3);

    // Подсказка "+N за клик"
    char gain[32];
    int p = itoa_(click_gain(), gain);
    p = sappend(gain, p, " / click", 32);
    text::draw(gain, cx + 16, cy + 64, 0x9CA3AF, 1);

    // Если авто-клик активен — показываем
    if (g_auto_click) {
        char ac[48];
        int pp = itoa_(g_auto_level + 1, ac);
        pp = sappend(ac, pp, " lvl auto", 48);
        text::draw(ac, cx + 16, cy + 80, 0x22C55E, 1);
    }

    // Основная Mimi (по центру)
    int mimi_x = cx + cw / 2;
    int mimi_y = cy + ch / 2 - 20;
    int mimi_r = 100;
    draw_mimi(mimi_x, mimi_y, mimi_r, g_cur_skin, squish_amount);

    // ---------- Магазин (правая колонка) ----------
    int sx = cx + cw - 260;
    int sy = cy + 16;

    text::draw("SKINS", sx, sy, 0x1F2937, 1);
    sy += 20;

    // Скины
    struct SkinBtn { const char* name; int id; int cost; bool owned; };
    SkinBtn skins[3] = {
        {"Base", 0, 0, true},
        {"Teen", 1, SKIN_COST_TEEN, g_teen_unlocked},
        {"Cool", 2, SKIN_COST_COOL, g_cool_unlocked}
    };

    for (int i = 0; i < 3; i++) {
        bool selected = (g_cur_skin == skins[i].id);
        bool can_buy = !skins[i].owned && g_coins >= skins[i].cost;
        bool active = selected || skins[i].owned || can_buy;

        u32 bg = selected ? 0x22C55E : (skins[i].owned ? 0xD1FAE5 : (can_buy ? 0x3B82F6 : 0xD1D5DB));
        u32 fg = selected || skins[i].owned || can_buy ? 0xFFFFFF : 0x8B93A1;

        gfx::round(sx, sy, 230, 32, 6, bg);
        text::draw(skins[i].name, sx + 10, sy + 10, fg, 1);

        if (skins[i].owned) {
            if (selected) text::draw("SELECTED", sx + 150, sy + 10, fg, 1);
            else text::draw("owned", sx + 170, sy + 10, fg, 1);
        } else {
            char cost[16]; itoa_(skins[i].cost, cost);
            text::draw(cost, sx + 200, sy + 10, fg, 1);
        }
        sy += 38;
    }

    sy += 10;
    text::draw("UPGRADES", sx, sy, 0x1F2937, 1);
    sy += 20;

    // Авто-клик
    bool auto_owned = g_auto_click;
    bool auto_buy = !auto_owned && g_coins >= AUTO_CLICK_COST;
    u32 auto_bg = auto_owned ? 0xD1FAE5 : (auto_buy ? 0x3B82F6 : 0xD1D5DB);
    u32 auto_fg = auto_owned || auto_buy ? (auto_owned ? 0x166534 : 0xFFFFFF) : 0x8B93A1;
    gfx::round(sx, sy, 230, 32, 6, auto_bg);
    text::draw("Auto-click", sx + 10, sy + 10, auto_fg, 1);
    if (auto_owned) text::draw("owned", sx + 170, sy + 10, auto_fg, 1);
    else { char c[8]; itoa_(AUTO_CLICK_COST, c); text::draw(c, sx + 200, sy + 10, auto_fg, 1); }
    sy += 38;

    // Скорость
    bool speed_owned = g_auto_click && g_auto_level >= 5;
    bool speed_buy = g_auto_click && !speed_owned && g_coins >= speed_cost();
    u32 sp_bg = speed_owned ? 0xD1FAE5 : (speed_buy ? 0x3B82F6 : (g_auto_click ? 0xD1D5DB : 0xE5E7EB));
    u32 sp_fg = speed_owned || speed_buy ? (speed_owned ? 0x166534 : 0xFFFFFF) : 0x8B93A1;
    gfx::round(sx, sy, 230, 32, 6, sp_bg);
    text::draw("Speed +1", sx + 10, sy + 10, sp_fg, 1);
    if (speed_owned) text::draw("MAX", sx + 190, sy + 10, sp_fg, 1);
    else if (g_auto_click) { char c[8]; itoa_(speed_cost(), c); text::draw(c, sx + 200, sy + 10, sp_fg, 1); }
    else text::draw("need auto", sx + 150, sy + 10, sp_fg, 1);
    sy += 38;

    // Мощность
    bool power_max = g_click_power >= 5;
    bool power_buy = !power_max && g_coins >= power_cost();
    u32 pw_bg = power_max ? 0xD1FAE5 : (power_buy ? 0x3B82F6 : 0xD1D5DB);
    u32 pw_fg = power_max || power_buy ? (power_max ? 0x166534 : 0xFFFFFF) : 0x8B93A1;
    gfx::round(sx, sy, 230, 32, 6, pw_bg);
    text::draw("Power +1", sx + 10, sy + 10, pw_fg, 1);
    if (power_max) text::draw("MAX", sx + 190, sy + 10, pw_fg, 1);
    else { char c[8]; itoa_(power_cost(), c); text::draw(c, sx + 200, sy + 10, pw_fg, 1); }
}

void clicker_click(int px, int py, int cx, int cy, int cw, int ch) {
    int mimi_x = cx + cw / 2;
    int mimi_y = cy + ch / 2 - 20;
    int mimi_r = 100;
    int r = mimi_r - squish_amount;

    // Клик по Mimi
    int dx = px - mimi_x, dy = py - mimi_y;
    if (dx * dx + dy * dy < r * r) {
        g_coins += click_gain();
        squish_amount = 15;
        squish_reversing = true;
        last_squish_tick = time_ms();
        beep(1200, 30);
        settings_save();
        return;
    }

    // Клик по магазину
    int sx = cx + cw - 260;
    int sy = cy + 16;

    // Скины: Base, Teen, Cool
    sy += 20;  // после "SKINS"
    for (int i = 0; i < 3; i++) {
        if (hit_rect(sx, sy, 230, 32, px, py)) {
            int cost = (i == 1) ? SKIN_COST_TEEN : (i == 2) ? SKIN_COST_COOL : 0;
            bool owned = (i == 0) || (i == 1 && g_teen_unlocked) || (i == 2 && g_cool_unlocked);
            if (owned) {
                g_cur_skin = i;
                beep(1000, 30);
            } else if (g_coins >= cost) {
                g_coins -= cost;
                if (i == 1) g_teen_unlocked = true;
                if (i == 2) g_cool_unlocked = true;
                g_cur_skin = i;
                beep(1500, 80);
            }
            settings_save();
            return;
        }
        sy += 38;
    }

    sy += 10 + 20;  // после "UPGRADES"

    // Auto-click
    if (hit_rect(sx, sy, 230, 32, px, py)) {
        if (!g_auto_click && g_coins >= AUTO_CLICK_COST) {
            g_coins -= AUTO_CLICK_COST;
            g_auto_click = true;
            beep(1500, 80);
            settings_save();
        }
        sy += 38;
        return;
    }
    sy += 38;

    // Speed
    if (hit_rect(sx, sy, 230, 32, px, py)) {
        if (g_auto_click && g_auto_level < 5 && g_coins >= speed_cost()) {
            g_coins -= speed_cost();
            g_auto_level++;
            beep(1500, 80);
            settings_save();
        }
        sy += 38;
        return;
    }
    sy += 38;

    // Power
    if (hit_rect(sx, sy, 230, 32, px, py)) {
        if (g_click_power < 5 && g_coins >= power_cost()) {
            g_coins -= power_cost();
            g_click_power++;
            beep(1500, 80);
            settings_save();
        }
    }
}