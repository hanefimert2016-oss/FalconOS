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

; ---- Experimental CPL3 return and INT 0x80 (not enabled in release) -------
%ifdef FALCON_RING3_TEST
section .bss
align 8
saved_ring_rsp:   resq 1
saved_ring_rbx:   resq 1
saved_ring_rbp:   resq 1
saved_ring_r12:   resq 1
saved_ring_r13:   resq 1
saved_ring_r14:   resq 1
saved_ring_r15:   resq 1

section .text
global ring3_enter
global ring3_syscall_int80
extern ring3_syscall_dispatch
ring3_enter:
    ; User code can clobber nonvolatile registers. Preserve kernel ABI.
    mov [rel saved_ring_rsp], rsp
    mov [rel saved_ring_rbx], rbx
    mov [rel saved_ring_rbp], rbp
    mov [rel saved_ring_r12], r12
    mov [rel saved_ring_r13], r13
    mov [rel saved_ring_r14], r14
    mov [rel saved_ring_r15], r15
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push qword 0x23
    push rsi
    pushfq
    pop rax
    or rax, 0x200           ; IF, allow PIC timer to continue running
    and rax, ~0x3000        ; IOPL=0, user cannot inb/outb
    push rax
    push qword 0x1B
    push rdi
    iretq

ring3_syscall_int80:
    PUSHA64
    ; On CPL3 entry, CPU used TSS.RSP0 and pushed SS/RSP/RFLAGS/CS/RIP.
    ; Full saved frame layout after PUSHA64: CS at rsp+128.
    mov rax, [rsp+128]
    and eax, 3
    cmp eax, 3
    jne .invalid
    mov rdi, [rsp+112]      ; user RAX syscall number
    mov rbx, rsp
    and rsp, -16
    cld
    call ring3_syscall_dispatch
    mov rsp, rbx
    cmp rax, 1
    je .leave_ring3         ; syscall 2 (exit)
    mov [rsp+112], rax
    POPA64
    iretq
.invalid:
    mov qword [rsp+112], -38
    POPA64
    iretq
.leave_ring3:
    mov rsp, [rel saved_ring_rsp]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rbx, [rel saved_ring_rbx]
    mov rbp, [rel saved_ring_rbp]
    mov r12, [rel saved_ring_r12]
    mov r13, [rel saved_ring_r13]
    mov r14, [rel saved_ring_r14]
    mov r15, [rel saved_ring_r15]
    ret
%endif
