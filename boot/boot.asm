; =============================================================================
;  FalconOS v1.0 — COLOR EDITION Bootloader
;  Architecture : x86 Real Mode (16-bit)
;  Assembler    : NASM
;  Video        : VGA Text Mode 3  (80×25, 16 colours)
;
;  Rendering    : INT 0x10 AH=0x13 (write string with attribute).
;                 Table-driven: each entry is  { row, col, attr, str..., NUL }
;                 Two gold eye-overlay passes use AH=0x02 + AH=0x09.
;
;  Colour map   :  0x0C = Bright Red   (dragon body)
;                  0x0E = Gold/Yellow  (dragon fire, eyes, version)
;                  0x0A = Bright Green (OS name)
;                  0x0B = Bright Cyan  (tagline)
;                  0x0F = Bright White (footer)
; =============================================================================

[BITS 16]
[ORG  0x7C00]

; ── Colour attribute constants ─────────────────────────────────────────────
RED   equ 0x0C
GOLD  equ 0x0E
GREEN equ 0x0A
CYAN  equ 0x0B
WHITE equ 0x0F

; =============================================================================
start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax          ; ES = 0 → INT 0x13 uses ES:BP as pointer to string
    mov  ss, ax
    mov  sp, 0x7C00
    sti

    ; ── Set VGA Text Mode 3 (80×25, 16-colour, clear screen) ────────────────
    mov  ax, 0x0003
    int  0x10

    ; ── Hide blinking hardware cursor ────────────────────────────────────────
    mov  ah, 0x01
    mov  cx, 0x2607      ; scan-line start > end → hidden
    int  0x10

    ; ── Explicitly clear screen: scroll-up 0 lines = full blank ─────────────
    mov  ah, 0x06
    xor  al, al          ; 0 = clear entire window
    mov  bh, 0x00        ; fill attribute: black background
    xor  cx, cx          ; top-left  (0,0)
    mov  dx, 0x184F      ; bottom-right (24,79)
    int  0x10

    ; ==========================================================================
    ;  Main rendering loop
    ;  Table format:  row:byte | col:byte | attr:byte | string… | NUL
    ;  Sentinel:      0xFF  (marks end of table)
    ; ==========================================================================
    mov  si, print_table   ; SI traverses the table

.next_entry:
    lodsb                  ; AL = row (or 0xFF sentinel)
    cmp  al, 0xFF
    je   .overlays_start

    mov  dh, al            ; DH = row

    lodsb
    mov  dl, al            ; DL = col

    lodsb
    mov  bl, al            ; BL = attribute (colour)

    mov  bp, si            ; ES:BP → first char of string (ES is still 0)

    ; ── Count NUL-terminated string length into CX ───────────────────────────
    xor  cx, cx
.count:
    cmp  byte [si], 0
    je   .counted
    inc  si
    inc  cx
    jmp  .count
.counted:
    inc  si                ; skip the NUL byte so SI is ready for next entry

    ; ── INT 0x10 / AH=0x13: Write String ────────────────────────────────────
    ;   AL=0  → write chars with BL attribute, do NOT advance cursor
    ;   BH=0  → page 0
    mov  ah, 0x13
    xor  al, al
    xor  bh, bh
    int  0x10

    jmp  .next_entry

    ; ==========================================================================
    ;  Gold overlay — dragon eyes at (row 6, col 10) and (row 6, col 16)
    ;  Uses AH=0x02 (position cursor) + AH=0x09 (write char+attr, no advance)
    ; ==========================================================================
.overlays_start:

    ; ── Left eye ─────────────────────────────────────────────────────────────
    mov  ah, 0x02
    xor  bh, bh
    mov  dh, 6
    mov  dl, 10
    int  0x10

    mov  ah, 0x09
    mov  al, 'O'
    xor  bh, bh
    mov  bl, GOLD
    mov  cx, 1
    int  0x10

    ; ── Right eye ────────────────────────────────────────────────────────────
    mov  ah, 0x02
    xor  bh, bh
    mov  dh, 6
    mov  dl, 16
    int  0x10

    mov  ah, 0x09
    mov  al, 'O'
    xor  bh, bh
    mov  bl, GOLD
    mov  cx, 1
    int  0x10

    ; ── Highlight centre snout squiggle (~) at (row 8, col 10..12) in GOLD ──
    mov  ah, 0x02
    xor  bh, bh
    mov  dh, 8
    mov  dl, 10
    int  0x10

    mov  ah, 0x09
    mov  al, '~'
    xor  bh, bh
    mov  bl, GOLD
    mov  cx, 3
    int  0x10

    ; ── Halt — spin forever ──────────────────────────────────────────────────
    jmp  $

; =============================================================================
;  Print table
;  Printed visual (representative, actual render depends on font/terminal):
;
;  col→  3                  25    42
;  row 3:       ___---___
;  row 4:     //  (   )  \\        FalconOS
;  row 5:    /  (  ~~~  )  \
;  row 6:   |  / O\   /O \  |     v1.0 - COLOR EDITION
;  row 7:   |  |   \ /   |  |
;  row 8:    \ |  _/~\_  | /
;  row 9:     \|_/     \_|/        Born to Fly
;  row 10:       |     |
;  row 11:      /|     |\
;  row 13:      ~~~~~~~~~           (GOLD fire)
;  row 22:  (c) 2026 FalconOS Project  (footer)
; =============================================================================
print_table:

    ; ── Dragon body — Bright Red (0x0C) ──────────────────────────────────────
    db  3,  3, RED
    db '      ___---___      ', 0

    db  4,  3, RED
    db '    //  (   )  \\    ', 0    ; '//' and '\\' are literal in sq-quotes

    db  5,  3, RED
    db '   /  (  ~~~  )  \   ', 0

    db  6,  3, RED
    db '  |  / O\   /O \  |  ', 0   ; O's will be overwritten gold by overlays

    db  7,  3, RED
    db '  |  |   \ /   |  |  ', 0

    db  8,  3, RED
    db '   \ |  _/~~~\_  | / ', 0   ; ~~~ will be overwritten gold by overlay

    db  9,  3, RED
    db '    \|_/       \_|/  ', 0

    db 10,  3, RED
    db '         |   |       ', 0

    db 11,  3, RED
    db '        /|   |\      ', 0

    ; ── Dragon fire — Gold (0x0E) ─────────────────────────────────────────────
    db 13,  3, GOLD
    db '      ~~~~~~~~~~~    ', 0

    ; ── OS Title — Bright Green (0x0A) ────────────────────────────────────────
    db  4, 42, GREEN
    db '   F a l c o n O S  ', 0

    ; ── Version line — Gold (0x0E) ────────────────────────────────────────────
    db  6, 42, GOLD
    db ' v1.0 - COLOR EDITION', 0

    ; ── Tagline — Bright Cyan (0x0B) ──────────────────────────────────────────
    db  9, 42, CYAN
    db '  << Born  to  Fly >>', 0

    ; ── Footer — Bright White (0x0F) ──────────────────────────────────────────
    db 22, 18, WHITE
    db '(c) 2026 FalconOS Project', 0

    db 0xFF   ; end-of-table sentinel

; =============================================================================
;  Pad to 510 bytes then write boot signature
; =============================================================================
times 510-($-$$) db 0
dw    0xAA55
