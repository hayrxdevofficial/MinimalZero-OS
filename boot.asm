bits 32

section .multiboot
align 4
    dd 0x1BADB002               ; magic
    dd 0x00000007               ; flags
    dd -(0x1BADB002 + 0x00000007) ; checksum
    dd 0, 0, 0, 0, 0
    dd 0                        ; linear graphics
    dd 480                      ; WIDTH (портрет)
    dd 800                      ; HEIGHT (портрет)
    dd 32                       ; bpp

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    push eax
    push ebx
    call kmain

hang:
    cli
    hlt
    jmp hang
