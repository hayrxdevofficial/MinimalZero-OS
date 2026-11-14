#include "common.h"

static u8 mimi_buf[MIMI_MAX];
static int mimi_sz = 0, mimi_stat = 0;
static char mimi_name[64];
static MimiArchive cur_mimi;

void installer_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, ch, 0xFFFFFF);
    text::draw("Installer — .mimi files", cx + 16, cy + 16, 0x1F2937, 1);

    if (mimi_stat == 0) {
        gfx::round(cx + 16, cy + 60, cw - 32, 120, 12, 0xF3F4F6);
        text::draw_c("Drop .mimi file here", cx + 16, cy + 80, cw - 32, 20, 0x6B7280, 1);
        text::draw_c("or download from Browser", cx + 16, cy + 110, cw - 32, 20, 0x9CA3AF, 1);
    } else if (mimi_stat > 0) {
        text::draw("Installed:", cx + 16, cy + 60, 0x22C55E, 1);
        text::draw_n(cur_mimi.app_name, cx + 16, cy + 90, 0x1F2937, 40);
        text::draw("Author:", cx + 16, cy + 130, 0x6B7280, 1);
        text::draw_n(cur_mimi.app_author, cx + 80, cy + 130, 0x1F2937, 30);
        btn({cx + 16, cy + ch - 60, 200, 40}, "Run", 0x22C55E, 0xFFFFFF);
        btn({cx + 226, cy + ch - 60, 200, 40}, "Install more", 0x3B82F6, 0xFFFFFF);
    } else {
        text::draw("Install error:", cx + 16, cy + 60, 0xEF4444, 1);
        const char* e = "";
        if (mimi_stat == -1) e = "Corrupt ZIP";
        if (mimi_stat == -2) e = "Missing app.js";
        if (mimi_stat == -3) e = "Missing icon.png";
        if (mimi_stat == -4) e = "Missing manifest.json";
        if (mimi_stat == -5) e = "Bad manifest format";
        text::draw(e, cx + 16, cy + 90, 0xEF4444, 1);
        btn({cx + 16, cy + ch - 60, 200, 40}, "Try again", 0x3B82F6, 0xFFFFFF);
    }
}

void installer_click(int px, int py, int cx, int cy, int cw, int ch) {
    if (mimi_stat > 0) {
        if (hit({cx + 16, cy + ch - 60, 200, 40}, px, py)) {
            net_run_js(cur_mimi.app_id); // Отправляем в JS
        }
        if (hit({cx + 226, cy + ch - 60, 200, 40}, px, py)) {
            mimi_stat = 0;
        }
    }
}