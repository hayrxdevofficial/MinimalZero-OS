#include "common.h"

int g_scr_w = 1024, g_scr_h = 768;

namespace gfx {
    static u8* fb = nullptr;
    static u32 W = 1024, H = 768, pitch = 0;
    static u8 bpp = 32;
    static bool ok = false;
    static u32 back[1024 * 768];

    void init(multiboot_info* mb) {
        if ((mb->flags & (1u << 12)) && mb->framebuffer_addr) {
            fb = (u8*)(u32)mb->framebuffer_addr;
            W = mb->framebuffer_width;
            H = mb->framebuffer_height;
            pitch = mb->framebuffer_pitch;
            bpp = mb->framebuffer_bpp;
            if (W > 1024) W = 1024;
            if (H > 768) H = 768;
            ok = (bpp == 32 || bpp == 24) && W >= 640 && H >= 480;
        }
    }

    bool is_ok() { return ok; }
    u32  width() { return W; }
    u32  height() { return H; }

    inline void put(int x, int y, u32 c) {
        if ((u32)x >= W || (u32)y >= H) return;
        back[y * W + x] = c;
    }

    inline u32 get(int x, int y) {
        if ((u32)x >= W || (u32)y >= H) return 0;
        return back[y * W + x];
    }

    inline u32 blend(u32 d, u32 s, u32 a) {
        u32 rb = (((d & 0xFF00FF) * (256 - a) + (s & 0xFF00FF) * a) >> 8) & 0xFF00FF;
        u32 g  = (((d & 0x00FF00) * (256 - a) + (s & 0x00FF00) * a) >> 8) & 0x00FF00;
        return rb | g;
    }

    u32 lerp(u32 c1, u32 c2, int t, int tmax) {
        if (tmax <= 0) return c1;
        int r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
        int r2 = (c2 >> 16) & 255, g2 = (c2 >> 8) & 255, b2 = c2 & 255;
        return (((r1 + (r2 - r1) * t / tmax) << 16) |
                ((g1 + (g2 - g1) * t / tmax) <<  8) |
                ( b1 + (b2 - b1) * t / tmax));
    }

    static bool clip(int& x, int& y, int& w, int& h) {
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > (int)W) w = (int)W - x;
        if (y + h > (int)H) h = (int)H - y;
        return w > 0 && h > 0;
    }

    void rect(int x, int y, int w, int h, u32 c) {
        if (!clip(x, y, w, h)) return;
        for (int j = 0; j < h; j++) {
            u32* row = &back[(y + j) * W + x];
            for (int i = 0; i < w; i++) row[i] = c;
        }
    }

    void rect_a(int x, int y, int w, int h, u32 c, u32 a) {
        if (!clip(x, y, w, h)) return;
        for (int j = 0; j < h; j++) {
            u32* row = &back[(y + j) * W + x];
            for (int i = 0; i < w; i++) row[i] = blend(row[i], c, a);
        }
    }

    void grad_v(int x, int y, int w, int h, u32 c1, u32 c2) {
        for (int j = 0; j < h; j++)
            rect(x, y + j, w, 1, lerp(c1, c2, j, h > 1 ? h - 1 : 1));
    }

    void grad_h(int x, int y, int w, int h, u32 c1, u32 c2) {
        if (!clip(x, y, w, h)) return;
        for (int i = 0; i < w; i++) {
            u32 c = lerp(c1, c2, i, w > 1 ? w - 1 : 1);
            for (int j = 0; j < h; j++) back[(y + j) * W + x + i] = c;
        }
    }

    static int isqrt_(int v) {
        if (v < 0) return 0;
        int r = 0; while ((r + 1) * (r + 1) <= v) r++;
        return r;
    }

    static int inset(int r, int dy) {
        int d = 2 * (r - dy) - 1;
        int v = 4 * r * r - d * d; if (v < 0) v = 0;
        return r - isqrt_(v) / 2;
    }

    void round(int x, int y, int w, int h, int r, u32 c) {
        if (r * 2 > h) r = h / 2;
        if (r * 2 > w) r = w / 2;
        for (int j = 0; j < h; j++) {
            int dy = j < h - 1 - j ? j : h - 1 - j;
            int ins = dy < r ? inset(r, dy) : 0;
            rect(x + ins, y + j, w - 2 * ins, 1, c);
        }
    }

    void round_a(int x, int y, int w, int h, int r, u32 c, u32 a) {
        if (r * 2 > h) r = h / 2;
        if (r * 2 > w) r = w / 2;
        for (int j = 0; j < h; j++) {
            int dy = j < h - 1 - j ? j : h - 1 - j;
            int ins = dy < r ? inset(r, dy) : 0;
            rect_a(x + ins, y + j, w - 2 * ins, 1, c, a);
        }
    }

    void outline(int x, int y, int w, int h, u32 c) {
        rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c);
        rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c);
    }

    void outline_round(int x, int y, int w, int h, int r, u32 c) {
        // аппроксимация — рисуем контур
        for (int j = 0; j < h; j++) {
            int dy = j < h - 1 - j ? j : h - 1 - j;
            int ins = dy < r ? inset(r, dy) : 0;
            put(x + ins, y + j, c);
            put(x + w - 1 - ins, y + j, c);
        }
        for (int i = 0; i < w; i++) {
            put(x + i, y, c);
            put(x + i, y + h - 1, c);
        }
    }

    void circle(int cx, int cy, int r, u32 c) {
        for (int j = -r; j <= r; j++) {
            int half = isqrt_(r * r - j * j);
            rect(cx - half, cy + j, half * 2 + 1, 1, c);
        }
    }

    void circle_a(int cx, int cy, int r, u32 c, u32 a) {
        for (int j = -r; j <= r; j++) {
            int half = isqrt_(r * r - j * j);
            rect_a(cx - half, cy + j, half * 2 + 1, 1, c, a);
        }
    }

    void ring(int cx, int cy, int r_out, int r_in, u32 c) {
        for (int j = -r_out; j <= r_out; j++) {
            int half_out = isqrt_(r_out * r_out - j * j);
            int half_in  = (j >= -r_in && j <= r_in) ? isqrt_(r_in * r_in - j * j) : 0;
            rect(cx - half_out, cy + j, half_out - half_in, 1, c);
            rect(cx + half_in + 1, cy + j, half_out - half_in, 1, c);
        }
    }

    inline u32 rgb565(u16 c) {
        u32 r = ((c >> 11) & 0x1F) * 255 / 31;
        u32 g = ((c >>  5) & 0x3F) * 255 / 63;
        u32 b = ( c        & 0x1F) * 255 / 31;
        return (r << 16) | (g << 8) | b;
    }

    // RLE-картинка (с совместимостью с прошлым форматом)
    void image(int ox, int oy, int dw, int dh, const u16* rle, u32 size, int iw, int ih) {
        u32 idx = 0; int x = 0, y = 0;
        while (idx < size && y < ih) {
            u16 hd = rle[idx++];
            u32 len = hd & 0x7FFF;
            bool run = (hd & 0x8000) != 0;
            u32 col = 0;
            if (run) col = rgb565(rle[idx++]);
            for (u32 i = 0; i < len && y < ih; i++) {
                if (!run) col = rgb565(rle[idx++]);
                int x0 = ox + x * dw / iw, x1 = ox + (x + 1) * dw / iw;
                int y0 = oy + y * dh / ih, y1 = oy + (y + 1) * dh / ih;
                rect(x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1, col);
                if (++x >= iw) { x = 0; y++; }
            }
        }
    }

    void clear(u32 c) { rect(0, 0, W, H, c); }

    void swap() {
        if (!fb) return;
        if (bpp == 32) {
            u32 p32 = pitch / 4;
            for (u32 y = 0; y < H; y++) {
                u32* d = (u32*)fb + y * p32;
                u32* s = &back[y * W];
                for (u32 x = 0; x < W; x++) d[x] = s[x];
            }
        } else {
            for (u32 y = 0; y < H; y++) {
                u8* d = fb + y * pitch;
                u32* s = &back[y * W];
                for (u32 x = 0; x < W; x++) {
                    u32 c = s[x];
                    d[x*3]   = c & 255;
                    d[x*3+1] = (c >> 8) & 255;
                    d[x*3+2] = (c >> 16) & 255;
                }
            }
        }
    }
}