#include "common.h"

#define COM1 0x3F8

static void serial_init() {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static bool serial_ready_r() { return (inb(COM1 + 5) & 1) != 0; }
static bool serial_ready_w() { return (inb(COM1 + 5) & 0x20) != 0; }

static void serial_write(u8 b) {
    int t = 1000000;
    while (!serial_ready_w() && --t);
    outb(COM1, b);
}

int net_poll_byte() {
    if (!serial_ready_r()) return -1;
    return inb(COM1);
}

void net_init() { serial_init(); }

// Порт 0xE9: команды в JS
// 0x01 [URL\0]                    — открыть URL в браузере
// 0x02 [URL\0][filename\0]        — скачать файл
// 0x03                            — закрыть браузер
// 0x04 [app_name\0]               — запустить JS-приложение
// 0x05                            — запросить список установленных .mimi

void net_open_url(const char* url) {
    outb(0xE9, 0x01);
    for (int i = 0; url[i]; i++) outb(0xE9, (u8)url[i]);
    outb(0xE9, 0);
}

void net_download(const char* url, const char* saveas) {
    outb(0xE9, 0x02);
    for (int i = 0; url[i]; i++) outb(0xE9, (u8)url[i]);
    outb(0xE9, 0);
    for (int i = 0; saveas[i]; i++) outb(0xE9, (u8)saveas[i]);
    outb(0xE9, 0);
}

void net_close_browser() {
    outb(0xE9, 0x03);
}

void net_run_js(const char* app_name) {
    outb(0xE9, 0x04);
    for (int i = 0; app_name[i]; i++) outb(0xE9, (u8)app_name[i]);
    outb(0xE9, 0);
}

// Приём файла: 4 байта размера LE + данные
int net_receive_file(u8* out, int max_sz) {
    int t = 50000000;
    u32 sz = 0;
    for (int i = 0; i < 4; i++) {
        int b;
        while ((b = net_poll_byte()) < 0) { if (--t == 0) return -1; }
        sz |= ((u32)(u8)b) << (i * 8);
    }
    if (sz > (u32)max_sz) return -2;
    for (u32 i = 0; i < sz; i++) {
        int b;
        while ((b = net_poll_byte()) < 0) { if (--t == 0) return -1; }
        out[i] = (u8)b;
    }
    return (int)sz;
}