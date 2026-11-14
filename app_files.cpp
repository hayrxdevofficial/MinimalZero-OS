#include "common.h"
#include "apps.h"

// ============================================================
//  Files — файловый менеджер
// ============================================================

static int fm_dir = 0, fm_sel = -1, fm_scroll = 0;

// ---- Хелперы для кнопок (ВСЕГДА ДО ИСПОЛЬЗОВАНИЯ) ----
static R fm_btn(int i, int cx, int cy) {
    static const int xs[5] = {10, 120, 230, 340, 450};
    return { cx + xs[i], cy + 6, 100, 28 };
}

static R fm_side(int i, int cx, int cy) {
    return { cx, cy + 10 + i * 34, 130, 32 };
}

static R fm_row(int i, int cx, int cy, int cw) {
    return { cx + 130, cy + 66 + i * 30, cw - 130, 30 };
}

// ---- Отрисовка ----
void files_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, ch, 0xF4F6FA);
    gfx::rect(cx, cy, 130, ch, 0xE3E8F2);

    static const char* side_names[5] = {"Home", "Docs", "Pics", "Music", "Trash"};
    int side_ids[5] = {0, fs_docs, fs_pics, -1, -2};

    for (int i = 0; i < 5; i++) {
        R r = fm_side(i, cx, cy);
        bool cur = (i < 4 && fm_dir == side_ids[i]);
        if (cur) gfx::round(r.x + 6, r.y, r.w - 12, r.h, 6, 0x3B82F6);
        else if (hov(r)) gfx::round(r.x + 6, r.y, r.w - 12, r.h, 6, 0xD2DAEA);

        text::draw(side_names[i], r.x + 16, r.y + 12, cur ? 0xFFFFFF : 0x1F2937, 1);
    }

    int x0 = cx + 130;
    btn(fm_btn(0, cx, cy), "< Up", 0x3B82F6, 0xFFFFFF, fm_dir != 0);
    btn(fm_btn(1, cx, cy), "New File", 0x3B82F6, 0xFFFFFF);
    btn(fm_btn(2, cx, cy), "Delete", 0xEF4444, 0xFFFFFF, fm_sel > 0);

    int list[MAXN];
    int n = fs_list(fm_dir, list, MAXN);
    int rows = (ch - 66) / 30;

    for (int i = 0; i < rows && i + fm_scroll < n; i++) {
        int idx = list[i + fm_scroll];
        R r = fm_row(i, cx, cy, cw);

        if (idx == fm_sel) gfx::rect(r.x, r.y, r.w, r.h, 0xCFE0FF);
        else if (hov(r)) gfx::rect(r.x, r.y, r.w, r.h, 0xE8EEFA);

        if (fs[idx].dir) {
            gfx::round(r.x + 12, r.y + 6, 20, 16, 3, 0xF5C542);
        } else {
            gfx::rect(r.x + 14, r.y + 5, 17, 21, 0xFFFFFF);
            gfx::outline(r.x + 14, r.y + 5, 17, 21, 0x9AA3B2);
        }

        text::draw_n(fs[idx].name, r.x + 46, r.y + 11, 0x1F2937, 30);
    }

    if (n == 0) text::draw("(empty)", x0 + 20, cy + 90, 0x9CA3AF, 1);
}

// ---- Обработка кликов ----
void files_click(int px, int py, int cx, int cy, int cw, int ch) {
    int side_ids[5] = {0, fs_docs, fs_pics, -1, -2};

    for (int i = 0; i < 5; i++) {
        if (hit(fm_side(i, cx, cy), px, py)) {
            if (i == 4) {
                // Trash (заглушка)
            } else if (side_ids[i] >= 0) {
                fm_dir = side_ids[i];
                fm_sel = -1;
                fm_scroll = 0;
            }
            return;
        }
    }

    if (hit(fm_btn(0, cx, cy), px, py) && fm_dir != 0) {
        fm_sel = fm_dir;
        fm_dir = fs[fm_dir].parent;
        fm_scroll = 0;
        return;
    }
    if (hit(fm_btn(1, cx, cy), px, py)) {
        char nm[32];
        scopy(nm, "new.txt", 32);
        int n = fs_new(fm_dir, nm, false);
        fs_set_txt(n, "New file content.");
        return;
    }
    if (hit(fm_btn(2, cx, cy), px, py) && fm_sel > 0) {
        fs_destroy(fm_sel);
        fm_sel = -1;
        return;
    }

    int list[MAXN];
    int n = fs_list(fm_dir, list, MAXN);
    int rows = (ch - 66) / 30;

    for (int i = 0; i < rows && i + fm_scroll < n; i++) {
        if (hit(fm_row(i, cx, cy, cw), px, py)) {
            int idx = list[i + fm_scroll];
            fm_sel = idx;
            if (fs[idx].dir) {
                fm_dir = idx;
                fm_sel = -1;
                fm_scroll = 0;
            }
            return;
        }
    }
    fm_sel = -1;
}

void files_key(int k) {
    if (k == 27) return;
}