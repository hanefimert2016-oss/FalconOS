# =============================================================================
#  FalconOS — bare-metal build system
# -----------------------------------------------------------------------------
#  Targets:
#    all          build the kernel ELF (default)
#    iso          wrap kernel.elf into a bootable GRUB ISO
#    run          boot the ISO in QEMU (windowed, default 1080p)
#    run-fb       boot kernel.elf directly via QEMU's -kernel  (faster iter)
#    run-headless boot the ISO with `-display none -vga std` for QMP scripting
#    font         regenerate kernel/font_data.c from DejaVu (requires Pillow)
#    clean        remove all build artefacts
#
#  Resolution (build-time, controls Multiboot2 framebuffer request and the
#  back-buffer size baked into the kernel):
#    make iso RES=hd      → 1280×800
#    make iso RES=fhd     → 1920×1080  (default)
#    make iso RES=2k      → 2560×1440  (≈ 14 MB BSS)
#
#  GRUB negotiates the actual mode with the BIOS/VBE; the kernel adapts at
#  runtime to whatever the boot info reports.
# =============================================================================

CC          := gcc
LD          := ld
NASM        := nasm
QEMU        := qemu-system-i386

BUILD       := build
ISO_DIR     := $(BUILD)/iso

RES         ?= fhd
ifeq ($(RES),hd)
FB_W := 1280
FB_H := 800
else ifeq ($(RES),fhd)
FB_W := 1920
FB_H := 1080
else ifeq ($(RES),2k)
FB_W := 2560
FB_H := 1440
else
$(error RES must be one of: hd, fhd, 2k (got '$(RES)'))
endif

CFLAGS      := -m32 -ffreestanding -fno-pic -fno-stack-protector \
               -fno-builtin -nostdlib -nostdinc \
               -Wall -Wextra -Wno-unused-parameter \
               -O2 -Ikernel \
               -DFB_W=$(FB_W) -DFB_H=$(FB_H)
LDFLAGS     := -m elf_i386 -T linker.ld -nostdlib
NASMFLAGS   := -f elf32 -DFB_W=$(FB_W) -DFB_H=$(FB_H)

C_SRCS      := $(wildcard kernel/*.c)
C_OBJS      := $(C_SRCS:%.c=$(BUILD)/%.o)
ASM_OBJS    := $(BUILD)/boot/multiboot2.o $(BUILD)/boot/isr.o

KERNEL      := $(BUILD)/falcon.elf
ISO         := $(BUILD)/FalconOS.iso

# 32 MB VRAM lets the std VGA (Bochs VBE) drive 1080p and 2K at 32 bpp
QEMU_FLAGS    := -m 256M -no-reboot -no-shutdown -serial stdio \
                 -display sdl -vga std -global VGA.vgamem_mb=32
HEADLESS_FLAGS:= -m 256M -no-reboot -no-shutdown -serial stdio \
                 -display none -vga std -global VGA.vgamem_mb=32

.PHONY: all iso run run-fb run-headless font clean

all: $(KERNEL)

# ---- compile C ---------------------------------------------------------------
$(BUILD)/kernel/%.o: kernel/%.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

# ---- assemble nasm sources ----------------------------------------------------
$(BUILD)/boot/%.o: boot/%.asm | $(BUILD)/boot
	$(NASM) $(NASMFLAGS) $< -o $@

# ---- link kernel --------------------------------------------------------------
$(KERNEL): $(ASM_OBJS) $(C_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS)
	@echo "[OK] linked $@  ($$(wc -c < $@) bytes, RES=$(RES) → $(FB_W)×$(FB_H))"

# ---- ISO ----------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL)        $(ISO_DIR)/boot/falcon.elf
	cp boot/grub.cfg    $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null
	@echo "[OK] ISO   $@  (RES=$(RES))"

# ---- run ----------------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(KERNEL)
	$(QEMU) -kernel $< $(QEMU_FLAGS)

run-headless: $(ISO)
	$(QEMU) -cdrom $< $(HEADLESS_FLAGS)

# ---- font regeneration --------------------------------------------------------
font:
	python3 tools/genfont.py

# ---- helpers ------------------------------------------------------------------
$(BUILD) $(BUILD)/kernel $(BUILD)/boot:
	@mkdir -p $@

clean:
	rm -rf $(BUILD)
