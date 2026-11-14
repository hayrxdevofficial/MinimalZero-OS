#!/usr/bin/env python3
import sys
from PIL import Image

def to565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def rle_encode(pixels):
    out = []
    i, n = 0, len(pixels)
    while i < n:
        j = i
        while j < n and j - i < 0x7FFF and pixels[j] == pixels[i]:
            j += 1
        run = j - i
        if run >= 3:
            out.append(0x8000 | run)
            out.append(pixels[i])
            i = j
        else:
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

def process_image(src, name, f_out):
    img = Image.open(src).convert("RGB")
    MAX = 600
    if img.width > MAX or img.height > MAX:
        img.thumbnail((MAX, MAX), Image.LANCZOS)
    
    w, h = img.size
    px = [to565(*p) for p in img.getdata()]
    rle = rle_encode(px)
    
    f_out.write(f"static const unsigned short {name}_w = {w};\n")
    f_out.write(f"static const unsigned short {name}_h = {h};\n")
    f_out.write(f"static const unsigned int {name}_size = {len(rle)};\n")
    f_out.write(f"static const unsigned short {name}_rle[] = {{\n")
    for i, v in enumerate(rle):
        f_out.write(f"{v},")
        if i % 16 == 15: f_out.write("\n")
    f_out.write("\n};\n\n")
    print(f"[img2c] {name}: {w}x{h}, {len(rle)} u16")

with open("images.h", "w") as f:
    f.write("// auto-generated\n")
    process_image("img_base.jpg", "img_base", f)
    process_image("img_adult.jpg", "img_adult", f)
    process_image("img_cool.jpg", "img_cool", f)
    process_image("img_hayrulla.jpg", "img_hayrulla", f)
