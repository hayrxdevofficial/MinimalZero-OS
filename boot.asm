bits 32

section .multiboot
align 4
    dd 0x1BADB002               ; magic
    dd 0x00000007               ; flags (ALIGN | MEMINFO | VIDEO)
    dd -(0x1BADB002 + 0x00000007) ; checksum
    ; aout kludge
    dd 0, 0, 0, 0, 0
    ; video mode request
    dd 0                        ; linear graphics mode
    dd 800                      ; width
    dd 600                      ; height
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
    
    ; Передаем аргументы для kmain(mb, magic)
    push eax                    ; magic
    push ebx                    ; mb (multiboot info)
    call kmain

hang:
    cli
    hlt
    jmp hang
