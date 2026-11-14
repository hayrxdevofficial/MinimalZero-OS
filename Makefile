CXX      = g++ -m32
AS       = nasm
LD       = ld -m elf_i386
CFLAGS   = -m32 -ffreestanding -fno-exceptions -fno-rtti -O2 -Wall -Wextra \
           -mno-sse -mno-sse2 -mno-mmx -fno-builtin -fno-stack-protector \
           -fno-pic -fno-pie -std=c++11
LDFLAGS  = -m elf_i386 -T linker.ld

# Так как kernel.cpp теперь монолитный, libc.cpp нам больше не нужен!
OBJS = boot.o kernel.o 

all: imimi.iso

# 1. Скачиваем оригинальные картинки из GitHub
img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg:
	bash tools/fetch_splash.sh

# 2. Генерируем images.h из картинок
images.h: img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg tools/img2c.py
	python3 tools/img2c.py

# 3. Сборка загрузчика (boot.asm)
boot.o: boot.asm
	$(AS) -f elf32 $< -o $@

# 4. Сборка ядра (обязательно ждём images.h!)
kernel.o: kernel.cpp images.h
	$(CXX) $(CFLAGS) -c $< -o $@

# 5. Линковка в бинарник
imimi.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 6. Сборка ISO через GRUB
imimi.iso: imimi.bin grub.cfg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp imimi.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o imimi.iso iso 2>/dev/null
	@echo "✅ Сборка завершена: imimi.iso"

# Очистка скомпилированных файлов
clean:
	rm -f *.o *.bin *.iso images.h boot_splash.jpg
	rm -rf iso

# Полная очистка (включая скачанные картинки)
distclean: clean
	rm -f img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg

.PHONY: all clean distclean