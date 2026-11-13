bits 32

section .multiboot
align 4
    dd 0x1BADB002               ; magic
    dd 0x00000007               ; flags
    dd -(0x1BADB002 + 0x00000007) ; checksum
    dd 0, 0, 0, 0, 0
    dd 0
    dd 800
    dd 600
    dd 32

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kmain

; --- НОВАЯ ФУНКЦИЯ ДЛЯ ЗАГРУЗКИ IDT ---
global load_idt
load_idt:
    mov eax, [esp + 4]  ; берем аргумент из стека (указатель на IDT)
    lidt [eax]          ; загружаем IDT
    ret

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
