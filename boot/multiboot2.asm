; =============================================================================
;  FalconOS — Multiboot2 entry stub  (i386 protected mode)
;
;  GRUB parses the Multiboot2 header below, sets up a 32-bit linear
;  framebuffer at the requested resolution, and jumps to `_start` with
;  the boot info pointer in EBX and the magic value 0x36D76289 in EAX.
;
;  We push (eax, ebx) onto the stack and hand control to `kernel_main`.
; =============================================================================

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH      equ 0                              ; i386 protected mode
MB2_LEN       equ header_end - header_start
MB2_CHECK     equ -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

section .multiboot
align 8
header_start:
    dd  MB2_MAGIC
    dd  MB2_ARCH
    dd  MB2_LEN
    dd  MB2_CHECK

    ; ---- Framebuffer request tag (type=5) ----------------------------------
    align 8
fb_tag_start:
    dw  5                       ; type   = framebuffer
    dw  0                       ; flags  = required
    dd  fb_tag_end - fb_tag_start
    dd  1024                    ; preferred width
    dd  768                     ; preferred height
    dd  32                      ; preferred depth
fb_tag_end:

    ; ---- End tag (type=0) --------------------------------------------------
    align 8
    dw  0
    dw  0
    dd  8
header_end:

; -----------------------------------------------------------------------------
section .bss
align 16
stack_bottom:
    resb 16384                  ; 16 KiB kernel stack
stack_top:

; -----------------------------------------------------------------------------
section .text
global _start
extern kernel_main

_start:
    cli
    mov     esp, stack_top      ; set up our stack
    push    ebx                 ; arg2 — multiboot info ptr
    push    eax                 ; arg1 — multiboot magic
    cld
    call    kernel_main
.halt:
    cli
    hlt
    jmp     .halt
