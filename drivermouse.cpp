#include "common.h"

int g_mx = 512, g_my = 384;
u8  g_mbtn = 0;
bool g_press = false, g_release = false;
int g_px = 0, g_py = 0;
int g_mspeed = 1;

namespace mouse {
    u8 pkt[3]; int idx = 0;

    static void wait_w() { int t = 100000; while ((inb(0x64) & 2) && --t); }
    static bool wait_r() { int t = 100000; while (!(inb(0x64) & 1) && --t); return t > 0; }
    static void write(u8 v) { wait_w(); outb(0x64, 0xD4); wait_w(); outb(0x60, v); }
    static u8   read() { return wait_r() ? inb(0x60) : 0; }
    static void drain() { for (int i = 0; i < 100; i++) { if (!(inb(0x64) & 1)) break; inb(0x60); } }

    void init() {
        g_mx = g_scr_w / 2;
        g_my = g_scr_h / 2;
        drain();
        wait_w(); outb(0x64, 0xA8);
        wait_w(); outb(0x64, 0x20);
        u8 status = read();
        status &= ~0x20;
        status &= ~0x02;
        wait_w(); outb(0x64, 0x60);
        wait_w(); outb(0x60, status);
        write(0xF6); read();
        write(0xF4); read();
        drain();
    }

    bool feed(u8 b) {
        if (idx == 0 && !(b & 0x08)) return false;
        pkt[idx++] = b;
        if (idx < 3) return false;
        idx = 0;
        if (pkt[0] & 0xC0) return false;
        int dx = (int)pkt[1] - (((int)pkt[0] << 4) & 0x100);
        int dy = (int)pkt[2] - (((int)pkt[0] << 3) & 0x100);
        g_mx += dx * g_mspeed;
        g_my -= dy * g_mspeed;
        if (g_mx < 0) g_mx = 0;
        if (g_mx >= g_scr_w) g_mx = g_scr_w - 1;
        if (g_my < 0) g_my = 0;
        if (g_my >= g_scr_h) g_my = g_scr_h - 1;
        u8 old = g_mbtn;
        g_mbtn = pkt[0] & 7;
        if ((g_mbtn & 1) && !(old & 1)) { g_press = true; g_px = g_mx; g_py = g_my; }
        if (!(g_mbtn & 1) && (old & 1)) g_release = true;
        return true;
    }
}