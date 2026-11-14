#include "common.h"

// .mimi — ZIP-архив:
//   app.js          — код приложения (обязательно)
//   icon.png        — иконка (обязательно)
//   manifest.json   — манифест (обязательно)
//   README.txt      — описание (опционально)

#define MIMI_MAX      (4 * 1024 * 1024)
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
    // Из manifest.json
    char app_id[64];
    char app_name[64];
    char app_author[64];
    char app_version[16];
    char app_desc[256];
    // Иконка
    u32 icon_offset;
    u32 icon_size;
    // JS-код
    u32 js_offset;
    u32 js_size;
};

static u16 rd16(const u8* p) { return p[0] | (p[1] << 8); }
static u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

// Простой JSON-парсер для плоского объекта
static bool json_get(const char* json, int len, const char* key, char* out, int out_max) {
    char search[64];
    int klen = slen(key);
    if (klen + 3 >= 64) return false;
    search[0] = '"';
    for (int i = 0; i < klen; i++) search[i + 1] = key[i];
    search[klen + 1] = '"';
    search[klen + 2] = 0;

    int slen2 = slen(search);
    for (int i = 0; i + slen2 < len; i++) {
        bool match = true;
        for (int j = 0; j < slen2; j++)
            if (json[i + j] != search[j]) { match = false; break; }
        if (!match) continue;
        int p = i + slen2;
        while (p < len && (json[p] == ' ' || json[p] == ':' || json[p] == '\t')) p++;
        if (p >= len || json[p] != '"') continue;
        p++;
        int out_p = 0;
        while (p < len && json[p] != '"' && out_p < out_max - 1) {
            if (json[p] == '\\' && p + 1 < len) p++;
            out[out_p++] = json[p++];
        }
        out[out_p] = 0;
        return true;
    }
    return false;
}

static bool parse_mimi(MimiArchive* a, u8* buf, int size) {
    memset(a, 0, sizeof(MimiArchive));
    a->buf = buf;
    a->buf_size = size;
    a->n_files = 0;
    a->valid = false;

    int eocd = -1;
    for (int i = size - 22; i >= 0 && i >= size - 65557; i--) {
        if (buf[i]   == 0x50 && buf[i+1] == 0x4B &&
            buf[i+2] == 0x05 && buf[i+3] == 0x06) { eocd = i; break; }
    }
    if (eocd < 0) return false;

    u16 total = rd16(buf + eocd + 10);
    u32 cd_off = rd32(buf + eocd + 16);
    if (cd_off >= (u32)size) return false;

    int p = (int)cd_off;
    bool has_js = false, has_icon = false, has_manifest = false;

    for (int f = 0; f < total && f < MIMI_MAX_FILES; f++) {
        if (buf[p] != 0x50 || buf[p+1] != 0x4B ||
            buf[p+2] != 0x01 || buf[p+3] != 0x02) break;
        u16 method    = rd16(buf + p + 10);
        u32 comp_size = rd32(buf + p + 20);
        u32 uncomp    = rd32(buf + p + 24);
        u16 nlen      = rd16(buf + p + 28);
        u16 elen      = rd16(buf + p + 30);
        u16 clen      = rd16(buf + p + 32);
        u32 loff      = rd32(buf + p + 42);
        char name[96];
        int nl = nlen < 95 ? nlen : 95;
        for (int i = 0; i < nl; i++) name[i] = buf[p + 46 + i];
        name[nl] = 0;

        if (loff + 30 > (u32)size) { p += 46 + nlen + elen + clen; continue; }
        u16 lnlen = rd16(buf + loff + 26);
        u16 lelen = rd16(buf + loff + 28);
        u32 data_off = loff + 30 + lnlen + lelen;

        MimiFile& mf = a->files[a->n_files++];
        scopy(mf.name, name, 96);
        mf.offset = data_off;
        mf.size = uncomp;
        mf.comp_size = comp_size;
        mf.method = method;

        if (seq(name, "app.js")) { a->js_offset = data_off; a->js_size = uncomp; has_js = true; }
        if (seq(name, "icon.png")) { a->icon_offset = data_off; a->icon_size = uncomp; has_icon = true; }
        if (seq(name, "manifest.json")) has_manifest = true;

        p += 46 + nlen + elen + clen;
    }

    if (!has_js || !has_icon || !has_manifest) return false;

    // Парсим manifest.json (только для несжатых)
    for (int i = 0; i < a->n_files; i++) {
        if (!seq(a->files[i].name, "manifest.json")) continue;
        if (a->files[i].method != 0) break;
        const char* js = (const char*)(buf + a->files[i].offset);
        int jl = a->files[i].size;
        json_get(js, jl, "id",      a->app_id,      64);
        json_get(js, jl, "name",    a->app_name,    64);
        json_get(js, jl, "author",  a->app_author,  64);
        json_get(js, jl, "version", a->app_version, 16);
        json_get(js, jl, "desc",    a->app_desc,   256);
        break;
    }

    a->valid = true;
    return true;
}

// Возвращает:
//   0  = OK
//  -1  = не ZIP
//  -2  = нет app.js
//  -3  = нет icon.png
//  -4  = нет manifest.json
//  -5  = плохой manifest
int install_mimi(u8* data, int size, MimiArchive* out) {
    if (!parse_mimi(out, data, size)) {
        // Различаем: ZIP валиден?
        bool is_zip = false;
        for (int i = 0; i < size - 4; i++) {
            if (data[i]==0x50 && data[i+1]==0x4B &&
                data[i+2]==0x03 && data[i+3]==0x04) { is_zip = true; break; }
        }
        if (!is_zip) return -1;
        if (out->n_files == 0) return -1;
        bool hj = false, hi = false, hm = false;
        for (int i = 0; i < out->n_files; i++) {
            if (seq(out->files[i].name, "app.js"))        hj = true;
            if (seq(out->files[i].name, "icon.png"))      hi = true;
            if (seq(out->files[i].name, "manifest.json")) hm = true;
        }
        if (!hj) return -2;
        if (!hi) return -3;
        if (!hm) return -4;
        return -5;
    }
    return 0;
}