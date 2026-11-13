CC       = g++ -m32
AS       = nasm
LD       = ld -m elf_i386
CFLAGS   = -ffreestanding -fno-exceptions -fno-rtti -O2 -mno-sse -mno-sse2 -mno-mmx -fno-builtin -fno-stack-protector -Wall -Wextra

OBJS     = boot.o kernel.o

all: MinimalZero.iso

boot_image.jpg:
	bash tools/fetch_splash.sh $@

splash.h boot_splash.jpg: boot_image.jpg tools/img2c.py
	python3 tools/img2c.py boot_image.jpg splash.h boot_splash.jpg

boot.o: boot.asm
	$(AS) -f elf32 $< -o $@

kernel.o: kernel.cpp splash.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

MinimalZero.iso: kernel.bin grub.cfg boot_splash.jpg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp kernel.bin       iso/boot/
	cp boot_splash.jpg  iso/boot/
	cp grub.cfg         iso/boot/grub/
	grub-mkrescue -o MinimalZero.iso iso 2>/dev/null

clean:
	rm -f *.o *.bin *.iso splash.h boot_splash.jpg
	rm -rf iso

.PHONY: all clean
