#include "common.h"

namespace kbd {
    static bool shift = false, e0 = false, caps = false;
    static const char norm[] = "\0\x1b" "1234567890-=" "\b\t" "qwertyuiop[]" "\n\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
    static const char shf[]  = "\0\x1b" "!@#$%^&*()_+" "\b\t" "QWERTYUIOP{}" "\n\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

    void feed(u8 b) {
        if (b == 0xE0) { e0 = true; return; }
        bool rel = (b & 0x80) != 0;
        u8 c = b & 0x7F;
        if (c == 0x2A || c == 0x36) { if (!e0) shift = !rel; e0 = false; return; }
        if (rel) { e0 = false; return; }
        if (e0) {
            e0 = false;
            if (c == 0x48) on_key(K_UP);
            else if (c == 0x50) on_key(K_DOWN);
            else if (c == 0x4B) on_key(K_LEFT);
            else if (c == 0x4D) on_key(K_RIGHT);
            else if (c == 0x53) on_key(K_DEL);
            else if (c == 0x47) on_key(K_HOME);
            else if (c == 0x4F) on_key(K_END);
            else if (c == 0x49) on_key(K_PGUP);
            else if (c == 0x51) on_key(K_PGDN);
            else if (c == 0x1C) on_key('\n');
            else if (c == 0x35) on_key('/');
            return;
        }
        if (c == 0x4E) { on_key('+'); return; }
        if (c == 0x4A) { on_key('-'); return; }
        {
            static const char kp[] = "789\0" "456\0" "123" "0.";
            if (c >= 0x47 && c <= 0x53 && c != 0x4A && c != 0x4E) {
                char d = kp[c - 0x47];
                if (d) { on_key(d); return; }
            }
        }
        if (c == 0x3A) { caps = !caps; return; }
        if (c == 0x3B) { on_key(K_F1); return; }
        if (c == 0x3C) { on_key(K_F2); return; }
        if (c == 0x3D) { on_key(K_F3); return; }
        if (c == 0x3E) { on_key(K_F4); return; }
        if (c == 0x3F) { on_key(K_F5); return; }
        if (c == 0x40) { on_key(K_F6); return; }
        if (c == 0x41) { on_key(K_F7); return; }
        if (c == 0x42) { on_key(K_F8); return; }
        if (c == 0x43) { on_key(K_F9); return; }
        if (c == 0x44) { on_key(K_F10); return; }
        if (c == 0x57) { on_key(K_F11); return; }
        if (c == 0x58) { on_key(K_F12); return; }
        if (c == 0x0F) { on_key(K_TAB); return; }
        if (c >= 58) return;
        char ch = shift ? shf[c] : norm[c];
        if (caps && ch >= 'a' && ch <= 'z') ch -= 32;
        else if (caps && ch >= 'A' && ch <= 'Z' && shift) ch += 32;
        if (ch) on_key((unsigned char)ch);
    }
}