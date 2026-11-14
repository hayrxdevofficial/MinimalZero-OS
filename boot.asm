bits 32

section .multiboot
align 4
    dd 0x1BADB002               ; magic
    dd 0x00000007               ; flags
    dd -(0x1BADB002 + 0x00000007) ; checksum
    dd 0, 0, 0, 0, 0
    dd 0                        ; linear graphics
    dd 1024                     ; WIDTH (ПК, 4:3)
    dd 768                      ; HEIGHT
    dd 32                       ; bpp

section .bss
align 16
stack_bottom:
    resb 65536
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
