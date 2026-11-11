.RECIPEPREFIX = >
CXX ?= g++
CXXFLAGS = -m32 -std=c++17 -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -Wall -Wextra

all: minimalzero.iso

boot.o: boot.S
> $(CXX) -m32 -c boot.S -o boot.o

kernel.o: kernel.cpp
> $(CXX) $(CXXFLAGS) -c kernel.cpp -o kernel.o

kernel.bin: boot.o kernel.o linker.ld
> ld -m elf_i386 -T linker.ld -nostdlib boot.o kernel.o -o kernel.bin

minimalzero.iso: kernel.bin grub.cfg
> mkdir -p iso/boot/grub
> cp kernel.bin iso/boot/kernel.bin
> cp grub.cfg iso/boot/grub/grub.cfg
> grub-mkrescue -o minimalzero.iso iso

clean:
> rm -rf *.o kernel.bin iso minimalzero.iso
