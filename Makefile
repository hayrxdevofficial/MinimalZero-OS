CXX = g++ -m32
AS = nasm
LD = ld -m elf_i386
CFLAGS = -m32 -ffreestanding -fno-exceptions -fno-rtti -O2 -Wall -Wextra -mno-sse -mno-sse2 -mno-mmx -fno-builtin -fno-stack-protector -fno-pic -fno-pie -std=c++11
LDFLAGS = -m elf_i386 -T linker.ld
OBJS = boot.o kernel.o

all: imimi.iso

img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg:
	bash tools/fetch_splash.sh

images.h: img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg tools/img2c.py
	python3 tools/img2c.py

boot.o: boot.asm
	$(AS) -f elf32 $< -o $@

kernel.o: kernel.cpp images.h
	$(CXX) $(CFLAGS) -c $< -o $@

imimi.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

imimi.iso: imimi.bin grub.cfg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp imimi.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o imimi.iso iso 2>/dev/null

clean:
	rm -f *.o *.bin *.iso images.h
	rm -rf iso

distclean: clean
	rm -f img_base.jpg img_adult.jpg img_cool.jpg img_hayrulla.jpg

.PHONY: all clean distclean