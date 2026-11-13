#!/usr/bin/env bash
# Скачивает splash-картинку из GitHub. Идемпотентно (кэширует).
set -euo pipefail

URL="https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/Assets.xcassets/AppIcon.appiconset/IMG_1710.jpeg"
OUT="${1:-boot_image.jpg}"

if [[ -f "$OUT" ]]; then
    echo "[fetch_splash] $OUT уже есть, пропускаю"
    exit 0
fi

echo "[fetch_splash] скачиваю $URL"
if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "$OUT" "$URL"
elif command -v wget >/dev/null; then
    wget -O "$OUT" "$URL"
else
    echo "Нужен curl или wget" >&2; exit 1
fi

# Проверка, что это действительно JPEG
if ! file "$OUT" | grep -qi jpeg; then
    echo "Ошибка: скачался не JPEG" >&2; exit 1
fi
echo "[fetch_splash] готово: $(du -h "$OUT" | cut -f1)"
