#include "common.h"

#define MAX_TABS 8
struct Tab { char url[256]; char title[64]; };
static Tab tabs[MAX_TABS];
static int tab_cnt = 0, act_tab = 0;
static char url_in[256];
static int url_len = 0;
static bool url_focus = true;

static void br_add_tab(const char* url) {
    if (tab_cnt >= MAX_TABS) return;
    scopy(tabs[tab_cnt].url, url, 256);
    int i = 0; const char* p = url;
    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p') {
        while (*p && *p != ':') p++;
        if (*p) p++; if (*p) p++; if (*p) p++;
    }
    while (*p && *p != '/' && i < 63) tabs[tab_cnt].title[i++] = *p++;
    tabs[tab_cnt].title[i] = 0;
    tab_cnt++; act_tab = tab_cnt - 1;
    net_open_url(url);
}

static void br_nav() {
    if (url_len == 0) return;
    url_in[url_len] = 0;
    char full[280];
    if (url_in[0]!='h'||url_in[1]!='t'||url_in[2]!='t'||url_in[3]!='p') {
        int j = 0; const char* pre = "https://";
        for (int i = 0; pre[i]; i++) full[j++] = pre[i];
        for (int i = 0; url_in[i] && j < 279; i++) full[j++] = url_in[i];
        full[j] = 0;
    } else scopy(full, url_in, 280);
    if (tab_cnt == 0) br_add_tab(full);
    else { scopy(tabs[act_tab].url, full, 256); net_open_url(full); }
    url_len = 0; url_in[0] = 0;
}

void browser_draw(int cx, int cy, int cw, int ch) {
    gfx::rect(cx, cy, cw, 32, 0x1F2937); int tx = cx + 4;
    for (int i = 0; i < tab_cnt; i++) {
        R tr = { tx, cy + 4, 120, 26 };
        gfx::round(tr.x, tr.y, tr.w, tr.h, 6, i == act_tab ? 0x374151 : 0x111827);
        text::draw_n(tabs[i].title, tr.x + 8, tr.y + 6, 0xFFFFFF, 12);
        text::draw("x", tr.x + tr.w - 18, tr.y + 6, hov({tr.x + tr.w - 20, tr.y + 4, 18, 20}) ? 0xEF4444 : 0x9CA3AF, 1);
        tx += 124;
    }
    if (tab_cnt < MAX_TABS) {
        R pr = { tx, cy + 4, 30, 26 };
        gfx::round(pr.x, pr.y, pr.w, pr.h, 6, hov(pr) ? 0x4B5563 : 0x374151);
        text::draw_c("+", pr.x, pr.y, pr.w, pr.h, 0xFFFFFF, 1);
    }
    gfx::rect(cx, cy + 32, cw, 44, 0xE5E7EB);
    R ub = { cx + 8, cy + 38, cw - 16, 32 };
    gfx::round(ub.x, ub.y, ub.w, ub.h, 16, url_focus ? 0xFFFFFF : 0xF3F4F6);
    gfx::outline(ub.x, ub.y, ub.w, ub.h, url_focus ? 0x3B82F6 : 0xD1D5DB);
    if (url_len == 0 && !url_focus) text::draw("Enter URL (press Enter)", ub.x + 30, ub.y + 8, 0x9CA3AF, 1);
    else text::draw_n(url_in, ub.x + 30, ub.y + 8, 0x1F2937, 50);
    if (url_focus && (time_ms() / 500) % 2 == 0) text::draw_char('|', ub.x + 30 + url_len * 8, ub.y + 8, 0x1F2937, 1);

    gfx::rect(cx, cy + 76, cw, ch - 76, 0xFFFFFF);
    if (tab_cnt == 0) {
        text::draw_c("Start typing URL", cx, cy + 100, cw, ch - 150, 0x9CA3AF, 1);
        text::draw_c("YouTube, Wikipedia, DuckDuckGo", cx, cy + 130, cw, ch - 150, 0x3B82F6, 1);
    } else {
        text::draw_c("Browser is open in fullscreen", cx, cy + 100, cw, ch - 150, 0x1F2937, 1);
        text::draw_c("Press Esc to return to OS", cx, cy + 124, cw, ch - 150, 0x6B7280, 1);
        char u[280]; scopy(u, "URL: ", 280); sappend(u, 5, tabs[act_tab].url, 280);
        text::draw_c(u, cx, cy + 160, cw, ch - 150, 0x3B82F6, 1);
    }
    btn({ cx + 16, cy + ch - 60, 120, 36 }, "YouTube", 0xEF4444, 0xFFFFFF);
    btn({ cx + 146, cy + ch - 60, 120, 36 }, "Wikipedia", 0x000000, 0xFFFFFF);
    btn({ cx + 276, cy + ch - 60, 120, 36 }, "DuckDuckGo", 0xDE5833, 0xFFFFFF);
}

void browser_click(int px, int py, int cx, int cy, int cw, int ch) {
    int tx = cx + 4;
    for (int i = 0; i < tab_cnt; i++) {
        if (hit({tx, cy + 4, 120, 26}, px, py)) {
            if (hit({tx + 100, cy + 4, 20, 26}, px, py)) {
                for (int k = i; k < tab_cnt - 1; k++) tabs[k] = tabs[k + 1];
                tab_cnt--; if (act_tab >= tab_cnt && tab_cnt > 0) act_tab = tab_cnt - 1;
                net_close_browser();
            } else { act_tab = i; net_open_url(tabs[i].url); }
            return;
        }
        tx += 124;
    }
    if (tab_cnt < MAX_TABS && hit({tx, cy + 4, 30, 26}, px, py)) { url_focus = true; return; }
    if (hit({cx + 8, cy + 38, cw - 16, 32}, px, py)) { url_focus = true; return; }
    if (hit({cx + 16, cy + ch - 60, 120, 36}, px, py)) { br_add_tab("https://www.youtube.com"); return; }
    if (hit({cx + 146, cy + ch - 60, 120, 36}, px, py)) { br_add_tab("https://ru.wikipedia.org"); return; }
    if (hit({cx + 276, cy + ch - 60, 120, 36}, px, py)) { br_add_tab("https://duckduckgo.com"); return; }
    url_focus = false;
}

void browser_key(int k) {
    if (url_focus) {
        if (k == '\n') { br_nav(); return; }
        if (k == '\b') { if (url_len > 0) { url_len--; url_in[url_len] = 0; } return; }
        if (k >= 32 && k < 127 && url_len < 255) { url_in[url_len++] = (char)k; return; }
    }
}