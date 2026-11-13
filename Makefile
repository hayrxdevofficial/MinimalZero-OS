CXX      = i686-elf-g++
AS       = i686-elf-as
LD       = i686-elf-ld
CFLAGS   = -m32 -ffreestanding -fno-exceptions -fno-rtti -O2 -Wall -Wextra
LDFLAGS  = -m elf_i386 -T linker.ld

OBJS     = boot.o kernel.o

all: imimi.iso

# --- Скачивание картинки (через bash — не зависит от chmod) ---
boot_image.jpg:
	bash tools/fetch_splash.sh $@

# --- Конвертация + сжатие ---
splash.h boot_splash.jpg: boot_image.jpg tools/img2c.py
	python3 tools/img2c.py boot_image.jpg splash.h boot_splash.jpg

boot.o: boot.S
	$(AS) --32 $< -o $@

kernel.o: kernel.cpp splash.h
	$(CXX) $(CFLAGS) -c $< -o $@

imimi.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

imimi.iso: imimi.bin grub.cfg boot_splash.jpg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp imimi.bin       iso/boot/
	cp boot_splash.jpg iso/boot/
	cp grub.cfg        iso/boot/grub/
	grub-mkrescue -o imimi.iso iso 2>/dev/null
	@echo "Готово: $$(du -h imimi.iso | cut -f1)"

run: imimi.iso
	qemu-system-i386 -cdrom imimi.iso -m 64M -vga std

clean:
	rm -f *.o *.bin *.iso splash.h boot_splash.jpg
	rm -rf iso

distclean: clean
	rm -f boot_image.jpg

.PHONY: all run clean distclean
