bits 32
section .multiboot
align 4
    dd 0x1BADB002
    dd 0x00000007
    dd -(0x1BADB002 + 0x00000007)
    dd 0, 0, 0, 0, 0
    dd 0
    dd 1024
    dd 768
    dd 32

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