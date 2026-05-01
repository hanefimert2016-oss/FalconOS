; =============================================================================
;  FalconOS — Multiboot2 entry stub  (v5: 32-bit → long mode handoff)
; -----------------------------------------------------------------------------
;  Multiboot2 always hands control over in 32-bit protected mode, so this stub
;  must be 32-bit even when we are building a 64-bit kernel.  The stub
;
;     1. Verifies the multiboot2 magic in EAX.
;     2. Builds a 4-level page table that identity-maps the first 1 GiB using
;        2-MiB pages (one PML4, one PDPT, one PD with 512 entries).
;     3. Enables PAE (CR4.PAE) and long mode (IA32_EFER.LME).
;     4. Loads CR3 with the PML4 address and turns paging on (CR0.PG), which
;        also activates long mode.
;     5. Loads a fresh 64-bit GDT and far-jumps into long mode at `long_start`,
;        passing the multiboot magic + info pointer in EDI/ESI per System-V.
;
;  Build flags (set by the Makefile):
;      -DFB_W=<width>   -DFB_H=<height>     preferred framebuffer geometry
; =============================================================================

%ifndef FB_W
%define FB_W 1920
%endif
%ifndef FB_H
%define FB_H 1080
%endif

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH      equ 0                              ; i386 protected mode
MB2_LEN       equ header_end - header_start
MB2_CHECK     equ -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

; -----------------------------------------------------------------------------
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
    dd  FB_W                    ; preferred width  (build-time)
    dd  FB_H                    ; preferred height (build-time)
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
align 4096
pml4:       resb 4096
pdpt:       resb 4096
pd:         resb 4096 * 4        ; 4 PDs → 4 GiB identity-mapped (covers FB)
align 16
mb2_magic:  resd 1
mb2_info:   resd 1
align 16
stack_bottom:
    resb 65536                  ; 64 KiB kernel stack
stack_top:

; -----------------------------------------------------------------------------
section .rodata
align 8
gdt64:
    dq 0                                    ; null
.code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53)      ; code64: present, ring0, exec, L
.data: equ $ - gdt64
    dq (1<<41)|(1<<44)|(1<<47)              ; data:   present, ring0, writable
.end:
gdt64_ptr:
    dw  gdt64.end - gdt64 - 1
    dq  gdt64

; -----------------------------------------------------------------------------
section .text
[bits 32]
global _start
extern long_start

_start:
    cli
    mov     esp, stack_top
    mov     [mb2_magic], eax    ; stash for the 64-bit world
    mov     [mb2_info],  ebx
    cld

    call    check_long_mode
    call    build_page_tables
    call    enable_long_mode

    ; ---- load 64-bit GDT and far-jump -------------------------------------
    lgdt    [gdt64_ptr]
    jmp     0x08:.in_long       ; selector = code64 (offset 0x08)

[bits 64]
.in_long:
    mov     ax, 0x10            ; data64 selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    mov     rsp, stack_top
    xor     rdi, rdi
    xor     rsi, rsi
    mov     edi, [mb2_magic]    ; 32-bit load → zero-extends to 64
    mov     esi, [mb2_info]
    call    long_start
.halt:
    cli
    hlt
    jmp     .halt

; -----------------------------------------------------------------------------
[bits 32]
check_long_mode:
    ; Detect CPUID support
    pushfd
    pop     eax
    mov     ecx, eax
    xor     eax, 1 << 21        ; toggle ID bit
    push    eax
    popfd
    pushfd
    pop     eax
    push    ecx
    popfd
    cmp     eax, ecx
    je      .nope

    ; Detect extended-function CPUID
    mov     eax, 0x80000000
    cpuid
    cmp     eax, 0x80000001
    jb      .nope

    ; Check long-mode bit (CPUID.80000001h:EDX[29])
    mov     eax, 0x80000001
    cpuid
    test    edx, 1 << 29
    jz      .nope
    ret
.nope:
    ; No long mode — sit in a halt loop.  We deliberately do not try to
    ; render anything since the framebuffer hand-shake comes after this.
    hlt
    jmp     .nope

build_page_tables:
    ; PML4[0] -> PDPT
    mov     eax, pdpt
    or      eax, 0x03           ; present + writable
    mov     [pml4], eax
    mov     dword [pml4 + 4], 0

    ; PDPT[0..3] -> PD[0..3]   (identity-map 4 GiB total, covers framebuffer)
    mov     ecx, 0
.pdpt_fill:
    mov     eax, ecx
    shl     eax, 12             ; eax = ecx * 4096
    add     eax, pd
    or      eax, 0x03           ; present + writable
    mov     [pdpt + ecx*8],     eax
    mov     dword [pdpt + ecx*8 + 4], 0
    inc     ecx
    cmp     ecx, 4
    jne     .pdpt_fill

    ; PDs: 2048 entries total (4 PDs × 512), each mapping 2 MiB (4 GiB total).
    mov     ecx, 0
.fill:
    mov     eax, 0x200000       ; 2 MiB
    mul     ecx                 ; EDX:EAX = ecx * 2 MiB
    or      eax, 0x83           ; present + writable + page-size (2 MiB)
    mov     [pd + ecx*8],     eax
    mov     [pd + ecx*8 + 4], edx
    inc     ecx
    cmp     ecx, 2048
    jne     .fill
    ret

enable_long_mode:
    ; Load PML4 into CR3
    mov     eax, pml4
    mov     cr3, eax

    ; Enable PAE (CR4.PAE = 1)
    mov     eax, cr4
    or      eax, 1 << 5
    mov     cr4, eax

    ; Set IA32_EFER.LME (long mode enable)
    mov     ecx, 0xC0000080
    rdmsr
    or      eax, 1 << 8
    wrmsr

    ; Enable paging (CR0.PG = 1) — this latches long mode active
    mov     eax, cr0
    or      eax, 1 << 31
    mov     cr0, eax
    ret
