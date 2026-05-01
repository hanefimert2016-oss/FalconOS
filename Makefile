# =============================================================================
#  FalconOS — bare-metal build system  (v5 "Aurora")
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
#  Architecture:
#    make iso ARCH=x86_64 (default)   →  64-bit long-mode kernel
#    make iso ARCH=i386               →  32-bit legacy build (v4-compatible)
#
#  Resolution (build-time, controls Multiboot2 framebuffer request and the
#  back-buffer size baked into the kernel):
#    make iso RES=hd      → 1280×800
#    make iso RES=fhd     → 1920×1080  (default)
#    make iso RES=2k      → 2560×1440  (≈ 14 MB BSS)
# =============================================================================

ARCH        ?= x86_64
RES         ?= fhd

# ---- per-architecture toolchain & flags -------------------------------------
ifeq ($(ARCH),x86_64)
CC          := gcc
LD          := ld
NASM        := nasm
QEMU        := qemu-system-x86_64
CFLAGS_ARCH := -m64 -mno-red-zone -mcmodel=kernel \
               -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
LDFLAGS_ARCH:= -m elf_x86_64
NASMFMT     := elf64
else ifeq ($(ARCH),i386)
CC          := gcc
LD          := ld
NASM        := nasm
QEMU        := qemu-system-i386
CFLAGS_ARCH := -m32
LDFLAGS_ARCH:= -m elf_i386
NASMFMT     := elf32
else
$(error ARCH must be one of: x86_64, i386 (got '$(ARCH)'))
endif

# ---- per-resolution geometry -------------------------------------------------
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

BUILD       := build
ISO_DIR     := $(BUILD)/iso

CFLAGS      := $(CFLAGS_ARCH) -ffreestanding -fno-pic -fno-stack-protector \
               -fno-builtin -nostdlib -nostdinc \
               -Wall -Wextra -Wno-unused-parameter \
               -O2 -Ikernel -Ilinux \
               -DFB_W=$(FB_W) -DFB_H=$(FB_H) -DARCH_$(ARCH)=1
LDFLAGS     := $(LDFLAGS_ARCH) -T linker.ld -nostdlib -z noexecstack
NASMFLAGS   := -f $(NASMFMT) -DFB_W=$(FB_W) -DFB_H=$(FB_H)

C_SRCS      := $(wildcard kernel/*.c) $(wildcard linux/*.c)
C_OBJS      := $(C_SRCS:%.c=$(BUILD)/%.o)
ASM_OBJS    := $(BUILD)/boot/multiboot2.o $(BUILD)/boot/isr.o

KERNEL      := $(BUILD)/falcon.elf
ISO         := $(BUILD)/FalconOS.iso

# 64 MB VRAM lets std VGA (Bochs VBE) drive 2K at 32 bpp comfortably
QEMU_FLAGS    := -m 512M -no-reboot -no-shutdown -serial stdio \
                 -display sdl -vga std -global VGA.vgamem_mb=64
HEADLESS_FLAGS:= -m 512M -no-reboot -no-shutdown -serial stdio \
                 -display none -vga std -global VGA.vgamem_mb=64

.PHONY: all iso run run-fb run-headless run-disk run-disk-headless wipe-disk font clean

all: $(KERNEL)

# ---- compile C ---------------------------------------------------------------
$(BUILD)/kernel/%.o: kernel/%.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/linux/%.o: linux/%.c kernel/falcon.h | $(BUILD)/linux
	$(CC) $(CFLAGS) -c $< -o $@

# ---- assemble nasm sources ----------------------------------------------------
$(BUILD)/boot/%.o: boot/%.asm | $(BUILD)/boot
	$(NASM) $(NASMFLAGS) $< -o $@

# ---- link kernel --------------------------------------------------------------
$(KERNEL): $(ASM_OBJS) $(C_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS)
	@echo "[OK] linked $@  ($$(wc -c < $@) bytes, ARCH=$(ARCH), RES=$(RES) → $(FB_W)×$(FB_H))"

# ---- ISO ----------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL)        $(ISO_DIR)/boot/falcon.elf
	cp boot/grub.cfg    $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null
	@echo "[OK] ISO   $@  (ARCH=$(ARCH), RES=$(RES))"

# ---- run ----------------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(KERNEL)
	$(QEMU) -kernel $< $(QEMU_FLAGS)

run-headless: $(ISO)
	$(QEMU) -cdrom $< $(HEADLESS_FLAGS)

# ---- persistent disk image: 64 MiB raw IDE drive on the primary master.
#  diskdb.c writes the user database + settings_t to LBA0 (4 sectors) so
#  user accounts and passwords survive cold reboots.
$(BUILD)/falcon.img: | $(BUILD)
	@if [ ! -f $@ ]; then \
	  qemu-img create -f raw $@ 64M; \
	  echo "[OK] new disk image $@ (64 MiB raw)"; \
	fi

run-disk: $(ISO) $(BUILD)/falcon.img
	$(QEMU) -cdrom $(ISO) -drive file=$(BUILD)/falcon.img,format=raw,if=ide,index=0 $(QEMU_FLAGS)

run-disk-headless: $(ISO) $(BUILD)/falcon.img
	$(QEMU) -cdrom $(ISO) -drive file=$(BUILD)/falcon.img,format=raw,if=ide,index=0 $(HEADLESS_FLAGS)

# Wipe the persistent disk image (forces installer wizard on next run-disk).
wipe-disk:
	rm -f $(BUILD)/falcon.img

# ---- font regeneration --------------------------------------------------------
font:
	python3 tools/genfont.py

# ---- helpers ------------------------------------------------------------------
$(BUILD) $(BUILD)/kernel $(BUILD)/boot $(BUILD)/linux:
	@mkdir -p $@

clean:
	rm -rf $(BUILD)
