#pragma once
#include "common.h"

#define MIMI_MAX       (4 * 1024 * 1024)
#define MIMI_MAX_FILES 32

struct MimiFile {
    char name[96];
    u32  offset;
    u32  size;
    u32  comp_size;
    u16  method;
};

struct MimiArchive {
    u8* buf;
    int buf_size;
    MimiFile files[MIMI_MAX_FILES];
    int n_files;
    bool valid;
    char app_id[64];
    char app_name[64];
    char app_author[64];
    char app_version[16];
    char app_desc[256];
    u32 icon_offset;
    u32 icon_size;
    u32 js_offset;
    u32 js_size;
};

// Функция установки (реализация в installerdriver.cpp)
int install_mimi(u8* data, int size, MimiArchive* out);