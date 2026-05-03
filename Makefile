# =============================================================================
#  FalconOS — bare-metal build system  (FalconOS 1)
# -----------------------------------------------------------------------------
#  Targets:
#    all          build the kernel ELF (default)
#    iso          wrap kernel.elf into a bootable GRUB ISO
#    run          boot the ISO in QEMU (windowed, 1080p screen)
#    run-fb       boot kernel.elf directly via QEMU's -kernel  (faster iter)
#    run-headless boot the ISO with `-display none -vga std` for QMP scripting
#    font         regenerate kernel/font_data.c from DejaVu (requires Pillow)
#    clean        remove all build artefacts
#
#  Architecture:
#    make iso ARCH=x86_64 (default)   →  64-bit long-mode kernel
#    make iso ARCH=i386               →  32-bit legacy build (v4-compatible)
#
#  Resolution:
#    The kernel always builds with a 2560 × 1440 back buffer so a single
#    ISO can boot at HD / FHD / 2K — pick the size from the GRUB menu, or
#    change it any time at runtime from Settings → Resolution.
# =============================================================================

ARCH        ?= x86_64

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

# ---- back-buffer geometry  (fixed at the maximum we ship) -------------------
#  GRUB picks the actual screen mode at boot and the kernel auto-adapts;
#  baking the back buffer at 2K means HD / FHD / 2K all render correctly
#  out of a single ISO. ~14 MB BSS, harmless on the 4 GiB QEMU box.
FB_W := 2560
FB_H := 1440

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

# 8 GB main RAM by default (override with `make run RAM=16384` for 16 GiB),
# 4 vCPUs, 128 MB VRAM — keeps 2K @ 32 bpp snappy and gives the planned
# in-memory FS a lot of headroom.  FalconOS itself only uses ~30 MB BSS.
RAM           ?= 8192
QEMU_FLAGS    := -m $(RAM)M -smp 4 -no-reboot -no-shutdown -serial stdio \
                 -display sdl -vga std -global VGA.vgamem_mb=128 \
                 -accel kvm -accel tcg
HEADLESS_FLAGS:= -m $(RAM)M -smp 4 -no-reboot -no-shutdown -serial stdio \
                 -display none -vga std -global VGA.vgamem_mb=128 \
                 -accel kvm -accel tcg

.PHONY: all iso run run-cdrom run-fb run-headless run-disk run-disk-headless wipe-disk font clean

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
	@echo "[OK] linked $@  ($$(wc -c < $@) bytes, ARCH=$(ARCH), back-buffer $(FB_W)×$(FB_H))"

# ---- ISO ----------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL)        $(ISO_DIR)/boot/falcon.elf
	cp boot/grub.cfg    $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null
	@echo "[OK] ISO   $@  (ARCH=$(ARCH), single ISO supports HD/FHD/2K via GRUB menu)"

# ---- run ----------------------------------------------------------------------
# `make run` now boots with a persistent 4 GiB disk attached so the installer
# actually has somewhere to write to — exactly like installing on real hardware.
# `make run-cdrom` keeps the old liveCD-only behaviour for quick smoke tests.
run: run-disk

run-cdrom: $(ISO)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(KERNEL)
	$(QEMU) -kernel $< $(QEMU_FLAGS)

run-headless: run-disk-headless

# ---- persistent disk image: 4 GiB raw IDE drive on the primary master.
#  diskdb.c writes the user database + settings_t to LBA0 (4 sectors) so
#  user accounts and passwords survive cold reboots.  4 GiB leaves room
#  for the future Files/prg backing store.
$(BUILD)/falcon.img: | $(BUILD)
	@if [ ! -f $@ ]; then \
	  qemu-img create -f raw $@ 4G; \
	  echo "[OK] new disk image $@ (4 GiB raw)"; \
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
