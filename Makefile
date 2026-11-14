CXX      = i686-elf-g++
AS       = i686-elf-as
LD       = i686-elf-ld
CFLAGS   = -m32 -ffreestanding -fno-exceptions -fno-rtti -O2 -Wall -Wextra \
           -mno-sse -mno-sse2 -mno-mmx -fno-builtin -fno-stack-protector \
           -fno-pic -std=c++11
LDFLAGS  = -m elf_i386 -T linker.ld

OBJS = boot.o kernel.o gfx.o text.o \
       drivermouse.o driverkeyboard.o drivernetwork.o installerdriver.o \
       app_files.o app_browser.o app_clicker.o app_calc.o app_settings.o app_installer.o

all: imimi.iso

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) -f elf32 $< -o $@

imimi.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

imimi.iso: imimi.bin grub.cfg
	@rm -rf iso
	mkdir -p iso/boot/grub
	cp imimi.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o imimi.iso iso 2>/dev/null

clean:
	rm -f *.o *.bin *.iso
	rm -rf iso

.PHONY: all clean