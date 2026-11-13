#!/usr/bin/env python3
# tools/img2c.py — из JPEG в C-заголовок с RLE-сжатием (RGB565).
import sys
from PIL import Image

SRC      = sys.argv[1] if len(sys.argv) > 1 else "boot_image.jpg"
DST_H    = sys.argv[2] if len(sys.argv) > 2 else "splash.h"
DST_GRUB = sys.argv[3] if len(sys.argv) > 3 else "boot_splash.jpg"

MAX_W, MAX_H = 800, 600

img = Image.open(SRC).convert("RGB")

# --- 1) Ресайз с сохранением пропорций ---
s = min(MAX_W / img.width, MAX_H / img.height, 1.0)
if s < 1.0:
    img = img.resize((int(img.width * s), int(img.height * s)), Image.LANCZOS)
w, h = img.size
print(f"[img2c] размер: {w}x{h}")

# --- 2) Для GRUB: отдельный JPEG уменьшенного размера ---
img.save(DST_GRUB, "JPEG", quality=85)
print(f"[img2c] GRUB-фон: {DST_GRUB}")

# --- 3) RGB565 ---
def to565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

px565 = [to565(*p) for p in img.getdata()]

# --- 4) RLE:  [count_hi_bit] [pixel...]
#   header & 0x8000 => run: (count & 0x7FFF) копий следующего пикселя
#   header & 0x8000 == 0 => literal: count пикселей подряд
def rle_encode(pixels):
    out = []
    i, n = 0, len(pixels)
    while i < n:
        # длина текущей серии
        j = i
        while j < n and j - i < 0x7FFF and pixels[j] == pixels[i]:
            j += 1
        run = j - i
        if run >= 3:
            out.append(0x8000 | run)
            out.append(pixels[i])
            i = j
        else:
            # литерал: тянем до начала серии из >=4
            k = i
            while k < n and k - i < 0x7FFF:
                if k + 3 < n and pixels[k] == pixels[k+1] == pixels[k+2] == pixels[k+3]:
                    break
                k += 1
            lit = k - i
            if lit == 0:
                out.append(0x8000 | 1); out.append(pixels[i]); i += 1
            else:
                out.append(lit)
                out.extend(pixels[i:k])
                i = k
    return out

rle = rle_encode(px565)
print(f"[img2c] RLE: {len(px565)} -> {len(rle)} u16 "
      f"({len(px565)*2//1024} КБ -> {len(rle)*2//1024} КБ)")

# --- 5) Запись заголовка ---
with open(DST_H, "w") as f:
    f.write(f"// auto-generated from {SRC} — do not edit\n")
    f.write(f"static const unsigned short splash_w = {w};\n")
    f.write(f"static const unsigned short splash_h = {h};\n")
    f.write(f"static const unsigned int   splash_rle_size = {len(rle)};\n")
    f.write("static const unsigned short splash_rle[] = {\n")
    for i, v in enumerate(rle):
        f.write(f"{v},")
        if i % 16 == 15:
            f.write("\n")
    f.write("\n};\n")
print(f"[img2c] заголовок: {DST_H}")
