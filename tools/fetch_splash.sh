#!/usr/bin/env bash
set -euo pipefail

URL_BASE="https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/Assets.xcassets/AppIcon.appiconset/IMG_1710.jpeg"
URL_TEEN="https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/mimi.imageset/IMG_1711.jpeg"
URL_COOL="https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/mimi.imageset/IMG_1748.jpeg"
URL_HAYR="https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/mimi.imageset/IMG_1752.jpeg"

fetch() {
    local url="$1"
    local out="$2"
    if [[ ! -f "$out" ]]; then
        echo "[fetch] Скачиваю $out..."
        curl -fsSL -o "$out" "$url"
    fi
}

fetch "$URL_BASE" "img_base.jpg"
fetch "$URL_TEEN" "img_adult.jpg"
fetch "$URL_COOL" "img_cool.jpg"
fetch "$URL_HAYR" "img_hayrulla.jpg"
echo "[fetch] Все картинки готовы."
