CC       = g++ -m32
AS       = nasm
LD       = ld -m elf_i386
CFLAGS   = -ffreestanding -fno-exceptions -fno-rtti -O2 -mno-sse -mno-sse2 -mno-mmx -fno-builtin -fno-stack-protector -Wall -Wextra

OBJS     = boot.o kernel.o

all: MinimalZero.iso

images.h: tools/fetch_splash.sh tools/img2c.py
	bash tools/fetch_splash.sh
	python3 tools/img2c.py

boot.o: boot.asm
	$(AS) -f elf32 $< -o $@

kernel.o: kernel.cpp images.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel.bin: $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

MinimalZero.iso: kernel.bin grub.cfg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp kernel.bin       iso/boot/
	cp grub.cfg         iso/boot/grub/
	grub-mkrescue -o MinimalZero.iso iso 2>/dev/null

clean:
	rm -f *.o *.bin *.iso images.h *.jpg
	rm -rf iso

.PHONY: all clean
