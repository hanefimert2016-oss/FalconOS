; =============================================================================
;  FalconOS — interrupt service routine stubs (32 exceptions + 16 IRQs)
; -----------------------------------------------------------------------------
;  Each stub pushes (err_code, vector) and jumps to a common trampoline that
;  saves general-purpose state, fixes up segment registers, calls the C-side
;  handler, then returns.  Function pointers are exposed via two arrays
;  (isr_table, irq_table) so the C layer can install them in the IDT in two
;  short loops.
; =============================================================================
[bits 32]

extern isr_handler
extern irq_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp  isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp  isr_common
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push dword 0
    push dword %2
    jmp  irq_common
%endmacro

isr_common:
    pusha
    push ds
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp                ; pointer to regs_t
    call isr_handler
    add  esp, 4
    pop  eax                ; old ds
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    popa
    add  esp, 8             ; pop vec + err
    iret

irq_common:
    pusha
    push ds
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp
    call irq_handler
    add  esp, 4
    pop  eax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    popa
    add  esp, 8
    iret

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

; ---- function-pointer tables consumed by kernel/idt.c ----------------------
section .data
global isr_table
isr_table:
%assign i 0
%rep 32
    dd isr %+ i
%assign i i+1
%endrep

global irq_table
irq_table:
%assign i 0
%rep 16
    dd irq %+ i
%assign i i+1
%endrep
