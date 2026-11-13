CXX      = i686-elf-g++
AS       = i686-elf-as
LD       = i686-elf-ld
CFLAGS   = -m32 -ffreestanding -fno-exceptions -fno-rtti -O2 -Wall -Wextra
LDFLAGS  = -m elf_i386 -T linker.ld

IMG_URL  = https://raw.githubusercontent.com/hayrxdevofficial/Mimi-Typh-App-IDK/main/Assets.xcassets/AppIcon.appiconset/IMG_1710.jpeg
OBJS     = boot.o kernel.o

all: imimi.iso

# --- Скачивание картинки ---
boot_image.jpg:
	./tools/fetch_splash.sh $@

# --- Конвертация + сжатие ---
splash.h boot_splash.jpg: boot_image.jpg tools/img2c.py
	python3 tools/img2c.py boot_image.jpg splash.h boot_splash.jpg

# --- Сборка ядра ---
boot.o: boot.S
	$(AS) --32 $< -o $@

kernel.o: kernel.cpp splash.h
	$(CXX) $(CFLAGS) -c $< -o $@

imimi.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# --- ISO с GRUB и обоими образами ---
imimi.iso: imimi.bin grub.cfg boot_splash.jpg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp imimi.bin        iso/boot/
	cp boot_splash.jpg  iso/boot/
	cp grub.cfg         iso/boot/grub/
	grub-mkrescue -o imimi.iso iso 2>/dev/null
	@echo "Готово: $(shell du -h imimi.iso | cut -f1)"

# --- Быстрый тест в QEMU ---
run: imimi.iso
	qemu-system-i386 -cdrom imimi.iso -m 64M -vga std

# --- Полная очистка (включая скачанное) ---
clean:
	rm -f *.o *.bin *.iso splash.h boot_splash.jpg
	rm -rf iso

distclean: clean
	rm -f boot_image.jpg

.PHONY: all run clean distclean
