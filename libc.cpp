#include "common.h"

extern "C" {

void* memset(void* d, int c, unsigned int n) {
    u8* p = (u8*)d;
    while (n--) { *p++ = (u8)c; }
    return d;
}

void* memcpy(void* d, const void* s, unsigned int n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    while (n--) { *a++ = *b++; }
    return d;
}

void* memmove(void* d, const void* s, unsigned int n) {
    u8* a = (u8*)d; const u8* b = (const u8*)s;
    if (a < b) { while (n--) { *a++ = *b++; } }
    else { a += n; b += n; while (n--) { *--a = *--b; } }
    return d;
}

int memcmp(const void* a, const void* b, unsigned int n) {
    const u8* x = (const u8*)a; const u8* y = (const u8*)b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}

}