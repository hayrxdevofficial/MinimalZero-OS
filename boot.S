# ============================================================
#  boot.S — Multiboot 1 + переход в kmain
#  Flags: ALIGN | MEMINFO | VIDEO => 0x1 | 0x2 | 0x4 = 0x7
# ============================================================

.set ALIGN,    1 << 0
.set MEMINFO,  1 << 1
.set VIDEO,    1 << 2
.set FLAGS,    ALIGN | MEMINFO | VIDEO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

# ---------- Multiboot header ----------
.section .multiboot
.align 4
.long MAGIC                 # 0x1BADB002
.long FLAGS                 # 0x00000007
.long CHECKSUM              # -(0x1BADB002 + 7)
# --- aout kludge (5 полей, все 0 — GRUB возьмёт из ELF) ---
.long 0                     # header_addr
.long 0                     # load_addr
.long 0                     # load_end_addr
.long 0                     # bss_end_addr
.long 0                     # entry_addr
# --- video mode request (4 поля) ---
.long 0                     # mode_type = 0 => linear graphics
.long 800                   # width
.long 600                   # height
.long 32                    # depth (bpp)

# ---------- Стек ----------
.section .bss
.align 16
stack_bottom:
    .skip 16384             # 16 KiB
stack_top:

# ---------- Точка входа ----------
.section .text
.global _start
.type _start, @function
_start:
    cli                     # прерывания пока не нужны
    mov $stack_top, %esp    # инициализируем стек

    # multiboot передаёт: EAX = magic, EBX = адрес multiboot_info
    push %ebx               # 2-й аргумент kmain
    push %eax               # 1-й аргумент kmain
    call kmain

    # если kmain вернулся — вешаемся
hang:
    cli
    hlt
    jmp hang

.size _start, . - _start
