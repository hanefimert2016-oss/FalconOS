; =============================================================================
;  FalconOS — interrupt service routine stubs (32 exceptions + 16 IRQs)
; -----------------------------------------------------------------------------
;  64-bit version.  Each stub pushes (vec, err) and jumps to a common
;  trampoline that saves the 16 GPRs, calls the C-side handler with `regs_t *`
;  in RDI (System-V AMD64), then restores state and IRETQ's.
;
;  Function-pointer tables (isr_table / irq_table) are exposed so kernel/idt.c
;  can install them in the 64-bit IDT in two short loops.
; =============================================================================
[bits 64]

extern isr_handler
extern irq_handler

%macro PUSHA64 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPA64 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp  isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1
    jmp  isr_common
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp  irq_common
%endmacro

isr_common:
    PUSHA64
    mov  rdi, rsp           ; arg1 → regs_t *
    cld
    call isr_handler
    POPA64
    add  rsp, 16            ; pop vec + err
    iretq

irq_common:
    PUSHA64
    mov  rdi, rsp
    cld
    call irq_handler
    POPA64
    add  rsp, 16
    iretq

; ---- 32 CPU-defined exceptions ---------------------------------------------
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; ---- 16 hardware IRQs (PIC remapped to 0x20..0x2F) -------------------------
IRQ 0,  0x20
IRQ 1,  0x21
IRQ 2,  0x22
IRQ 3,  0x23
IRQ 4,  0x24
IRQ 5,  0x25
IRQ 6,  0x26
IRQ 7,  0x27
IRQ 8,  0x28
IRQ 9,  0x29
IRQ 10, 0x2A
IRQ 11, 0x2B
IRQ 12, 0x2C
IRQ 13, 0x2D
IRQ 14, 0x2E
IRQ 15, 0x2F

; ---- function-pointer tables consumed by kernel/idt.c ---------------------
section .data
global isr_table
isr_table:
%assign i 0
%rep 32
    dq isr %+ i
%assign i i+1
%endrep

global irq_table
irq_table:
%assign i 0
%rep 16
    dq irq %+ i
%assign i i+1
%endrep
