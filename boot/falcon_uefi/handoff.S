.intel_syntax noprefix
.text
.globl falcon_jump_to_kernel
/* Win64 EFI: RCX kernel entry, RDX MB2, R8 CR3, R9 stack.
 * SysV kernel: EDI MB2 magic, RSI MB2 bootinfo. */
falcon_jump_to_kernel:
    cli
    mov r10, rcx
    mov r11, rdx
    mov cr3, r8
    lgdt [rip + gdtr64]
    lea rax, [rip + .Lgo]
    push 0x08
    push rax
    retfq
.Lgo:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, r9
    and rsp, -16
    xor rbp, rbp
    mov edi, 0x36D76289
    mov rsi, r11
    call r10
.Lhalt:
    cli
    hlt
    jmp .Lhalt
.section .rdata,"dr"
.align 8
gdt64:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdtr64:
    .short 23
    .quad gdt64
