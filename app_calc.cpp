#include "common.h"
#include "apps.h"

// ============================================================
//  Calculator — с памятью, операциями и историей
// ============================================================

static long long calc_display = 0;
static long long calc_stored = 0;
static int       calc_op = 0;        // 0=none, 1=+, 2=-, 3=*, 4=/
static bool      calc_new_num = true;
static bool      calc_error = false;
static char      calc_history[8][32];
static int       calc_hist_count = 0;

// Кнопки: 0-9, +, -, *, /, =, C, ., +/-
// Раскладка 4x5
static const char* key_labels[20] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "C", "0", ".", "+",
    "+/-", "=", "", ""
};

void calc_init() {
    calc_display = 0;
    calc_stored = 0;
    calc_op = 0;
    calc_new_num = true;
    calc_error = false;
}

static void add_history(const char* h) {
    if (calc_hist_count < 8) {
        scopy(calc_history[calc_hist_count++], h, 32);
    } else {
        for (int i = 0; i < 7; i++) scopy(calc_history[i], calc_history[i + 1], 32);
        scopy(calc_history[7], h, 32);
    }
}

static void calc_clear() {
    calc_display = 0;
    calc_stored = 0;
    calc_op = 0;
    calc_new_num = true;
    calc_error = false;
}

static void calc_eval() {
    if (!calc_op) return;
    long long a = calc_stored, b = calc_display, r = b;
    char hist[32];
    char tmp[16];

    itoa_((int)a, tmp); scopy(hist, tmp, 32);
    const char* op_str = (calc_op == 1) ? "+" : (calc_op == 2) ? "-" : (calc_op == 3) ? "*" : "/";
    sappend(hist, slen(hist), " ", 32);
    sappend(hist, slen(hist), op_str, 32);
    sappend(hist, slen(hist), " ", 32);
    itoa_((int)b, tmp); sappend(hist, slen(hist), tmp, 32);
    sappend(hist, slen(hist), " = ", 32);

    if (calc_op == 1) r = a + b;
    else if (calc_op == 2) r = a - b;
    else if (calc_op == 3) r = a * b;
    else if (calc_op == 4) {
        if (b == 0) { calc_error = true; r = 0; }
        else r = a / b;
    }

    itoa_((int)r, tmp); sappend(hist, slen(hist), tmp, 32);
    add_history(hist);

    calc_display = r;
    calc_op = 0;
    calc_new_num = true;
}

static void calc_digit(int d) {
    if (calc_error) calc_clear();
    if (calc_new_num) {
        calc_display = d;
        calc_new_num = false;
    } else {
        long long v = calc_display * 10 + d;
        if (v > -999999999 && v < 999999999) calc_display = v;
    }
}

static void calc_set_op(int op) {
    if (calc_error) calc_clear();
    if (calc_op && !calc_new_num) calc_eval();
    calc_stored = calc_display;
    calc_op = op;
    calc_new_num = true;
}

void calc_key(int k) {
    if (k >= '0' && k <= '9') { calc_digit(k - '0'); return; }
    if (k == '+') { calc_set_op(1); return; }
    if (k == '-') { calc_set_op(2); return; }
    if (k == '*') { calc_set_op(3); return; }
    if (k == '/') { calc_set_op(4); return; }
    if (k == '=' || k == '\n') { calc_eval(); return; }
    if (k == 'c' || k == 'C') { calc_clear(); return; }
    if (k == '\b') {
        if (!calc_new_num) {
            calc_display /= 10;
            if (calc_display == 0) calc_new_num = true;
        }
    }
}

// Раскладка: 4 колонки x 5 строк
static void calc_get_btn(int i, int cx, int cy, int cw, int ch, int& bx, int& by, int& bw, int& bh) {
    int col = i % 4, row = i / 4;
    int pad = 8;
    bw = (cw - pad * 5) / 4;
    bh = (ch - 100 - pad * 6) / 5;
    bx = cx + pad + col * (bw + pad);
    by = cy + 100 + pad + row * (bh + pad);
}

void calc_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, ch, 0x1B1F2B);

    // Экран
    gfx::round(cx + 12, cy + 12, cw - 24, 76, 8, 0x0F1218);

    char buf[24];
    if (calc_error) {
        scopy(buf, "ERROR", 24);
    } else {
        itoa_((int)calc_display, buf);
    }
    int tw = text::width(buf, 3);
    text::draw(buf, cx + cw - 24 - tw, cy + 40, 0xE5E7EB, 3);

    // Индикатор операции (верхний левый угол экрана)
    if (calc_op && !calc_error) {
        const char* op = (calc_op == 1) ? "+" : (calc_op == 2) ? "-" : (calc_op == 3) ? "*" : "/";
        text::draw(op, cx + 24, cy + 20, 0x9CA3AF, 2);
    }

    // Кнопки 4x4 (16 штук) — первые 16 из массива
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_get_btn(i, cx, cy, cw, ch, bx, by, bw, bh);

        u32 col = 0x3A4154;  // обычные
        if (i % 4 == 3) col = 0xF59E0B;  // операции (/, *, -, +)
        if (i == 12) col = 0xEF4444;     // C
        if (i == 13) col = 0x3A4154;     // 0
        if (i == 14) col = 0x3A4154;     // .
        if (i == 15) col = 0xF59E0B;     // +

        // Подсветка активной операции
        int op_id = (i == 3) ? 4 : (i == 7) ? 3 : (i == 11) ? 2 : (i == 15) ? 1 : 0;
        if (op_id && calc_op == op_id) col = 0xFFD166;

        bool hov = hit_rect(bx, by, bw, bh, g_mx, g_my);
        if (hov) col = gfx::lerp(col, 0xFFFFFF, 1, 5);

        gfx::round(bx, by, bw, bh, 8, col);

        u32 fg = (col == 0xFFD166) ? 0x000000 : 0xFFFFFF;
        text::draw_c(key_labels[i], bx, by, bw, bh, fg, 2);
    }

    // Кнопка "=" (занимает всю нижнюю строку)
    int bx, by, bw, bh;
    calc_get_btn(17, cx, cy, cw, ch, bx, by, bw, bh);
    // Растягиваем на 3 кнопки
    bw = bw * 3 + 16;  // + 2 * pad
    bool hov = hit_rect(bx, by, bw, bh, g_mx, g_my);
    gfx::round(bx, by, bw, bh, 8, hov ? 0x34D399 : 0x22C55E);
    text::draw_c("=", bx, by, bw, bh, 0xFFFFFF, 2);

    // Кнопка +/- (вторая снизу, слева)
    calc_get_btn(16, cx, cy, cw, ch, bx, by, bw, bh);
    hov = hit_rect(bx, by, bw, bh, g_mx, g_my);
    gfx::round(bx, by, bw, bh, 8, hov ? 0x6B7280 : 0x4B5563);
    text::draw_c("+/-", bx, by, bw, bh, 0xFFFFFF, 1);
}

void calc_click(int px, int py, int cx, int cy, int cw, int ch) {
    for (int i = 0; i < 16; i++) {
        int bx, by, bw, bh;
        calc_get_btn(i, cx, cy, cw, ch, bx, by, bw, bh);
        if (hit_rect(bx, by, bw, bh, px, py)) {
            beep(1500, 20);
            if (i >= 0 && i <= 9) {
                // цифры 7,8,9,4,5,6,1,2,3 — не по порядку
                static const int digits[16] = {7, 8, 9, -1, 4, 5, 6, -1, 1, 2, 3, -1, -1, 0, -2, -1};
                int d = digits[i];
                if (d >= 0) calc_digit(d);
                else if (d == -2) { /* . — пока не поддерживаем дробные */ }
            } else if (i == 12) calc_clear();
            else if (i == 3) calc_set_op(4);
            else if (i == 7) calc_set_op(3);
            else if (i == 11) calc_set_op(2);
            else if (i == 15) calc_set_op(1);
            return;
        }
    }

    // = 
    int bx, by, bw, bh;
    calc_get_btn(17, cx, cy, cw, ch, bx, by, bw, bh);
    bw = bw * 3 + 16;
    if (hit_rect(bx, by, bw, bh, px, py)) {
        beep(1200, 30);
        calc_eval();
        return;
    }

    // +/-
    calc_get_btn(16, cx, cy, cw, ch, bx, by, bw, bh);
    if (hit_rect(bx, by, bw, bh, px, py)) {
        calc_display = -calc_display;
        beep(1500, 20);
    }
}