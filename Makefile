# =============================================================================
#  FalconOS — bare-metal build system  (FalconOS 1)
# -----------------------------------------------------------------------------
#  Targets:
#    all            build the kernel ELF (default)
#    start / everything  build ISO + QEMU with 200G qcow2 demo disk (tek komut)
#    iso            wrap kernel.elf into a bootable GRUB ISO
#    run            same as run-disk (persistent qcow2 disk)
#    run-disk-ephemeral  QEMU -snapshot (guest writes discarded on exit)
#    run-fb         boot kernel.elf directly via QEMU's -kernel (faster iter)
#    run-headless   ISO + disk, no display
#    font           regenerate kernel/font_data.c from DejaVu (requires Pillow)
#    clean          remove all build artefacts
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

# ---- BearSSL (vendored) ------------------------------------------------------
# BearSSL is built with the same freestanding toolchain.  We point its
# <string.h> at our shim and let GCC's builtin freestanding headers
# satisfy <stddef.h> and <stdint.h>.
GCC_INC        := $(shell $(CC) -print-file-name=include)
BEARSSL_DIR    := vendor/bearssl
# Filter out the optional hardware-accelerated implementations: those
# need -msse2/-maes/-mpclmul/-mavx and pull in <x86intrin.h>, which
# transitively wants <stdlib.h>.  The kernel is built with -mno-sse, so
# we keep the constant-time portable variants instead.
BEARSSL_SRCS_ALL := $(shell find $(BEARSSL_DIR)/src -name '*.c')
# Only excludes things that physically can't compile under -nostdinc /
# -mno-sse: rand/sysrng.c (/dev/urandom + windows wincrypt) is the only
# source that includes platform headers unconditionally.  The other
# SIMD implementations are gated by BR_AES_X86NI / BR_GHASH_PCLMUL /
# etc. (defined to 0 in BEARSSL_CFLAGS below), so the SIMD files
# compile to empty translation units.
BEARSSL_EXCLUDE  := \
    $(BEARSSL_DIR)/src/rand/sysrng.c
BEARSSL_SRCS   := $(filter-out $(BEARSSL_EXCLUDE),$(BEARSSL_SRCS_ALL))
BEARSSL_OBJS   := $(BEARSSL_SRCS:%.c=$(BUILD)/%.o)
BEARSSL_CFLAGS := $(CFLAGS_ARCH) -ffreestanding -fno-pic -fno-stack-protector \
                  -fno-builtin -nostdlib -nostdinc -O2 \
                  -isystem $(GCC_INC) \
                  -I vendor/bearssl-shim \
                  -I $(BEARSSL_DIR)/inc -I $(BEARSSL_DIR)/src \
                  -Wno-unused-parameter -Wno-unused-but-set-variable \
                  -DBR_LOMUL=1 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
                  -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 \
                  -DBR_AES_X86NI=0 -DBR_GHASH_PCLMUL=0 \
                  -DBR_POWER8=0 -DBR_INT128=0 -DBR_UMUL128=0
SHIM_OBJ       := $(BUILD)/vendor/bearssl-shim/strops.o

KERNEL      := $(BUILD)/falcon.elf
ISO         := $(BUILD)/FalconOS.iso

# Omit -no-shutdown / -no-reboot so ACPI power-off (PW_REG) and keyboard reset
# behave like real hardware and terminate or restart the QEMU process.
RAM           ?= 12288
CPUS          ?= 6
VRAM          ?= 256
DISK_CAPACITY ?= 200G

QEMU_FLAGS    := -m $(RAM)M -smp $(CPUS) -serial stdio \
                 -display sdl -vga std -global VGA.vgamem_mb=$(VRAM) \
                 -accel kvm -accel tcg

HEADLESS_FLAGS:= -m $(RAM)M -smp $(CPUS) -serial stdio \
                 -display none -vga std -global VGA.vgamem_mb=$(VRAM) \
                 -accel kvm -accel tcg

.PHONY: start all iso everything run run-cdrom run-fb run-headless \
        run-disk run-disk-headless run-disk-ephemeral wipe-disk font clean

# Türkçe README’de de geçecek tek komut: ISO derle + QEMU (kalıcı qcow2 disk).
start: everything

all: $(KERNEL)

RUN_DISK_DRIVE := file=$(BUILD)/falcon.img,format=qcow2,if=ide,index=0

# ---- compile C ---------------------------------------------------------------
$(BUILD)/kernel/%.o: kernel/%.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/linux/%.o: linux/%.c kernel/falcon.h | $(BUILD)/linux
	$(CC) $(CFLAGS) -c $< -o $@

# ---- assemble nasm sources ----------------------------------------------------
$(BUILD)/boot/%.o: boot/%.asm | $(BUILD)/boot
	$(NASM) $(NASMFLAGS) $< -o $@

# ---- compile BearSSL ---------------------------------------------------------
# A single pattern rule covers all 277 .c files under vendor/bearssl/src/.
# The recipe lives in its own block (no kernel/falcon.h dependency).
$(BUILD)/vendor/bearssl/src/%.o: $(BEARSSL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BUILD)/vendor/bearssl-shim/%.o: vendor/bearssl-shim/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

# kernel/tls_client.c and kernel/tls_roots.c include bearssl.h, which
# wants <stddef.h>/<stdint.h>; reuse the BearSSL flag set so they pick
# up GCC's freestanding headers and our shim.  Falcon's own types come
# in through -Ikernel which BEARSSL_CFLAGS doesn't have, so re-add it.
$(BUILD)/kernel/tls_client.o: kernel/tls_client.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(BEARSSL_CFLAGS) -Ikernel -DFB_W=$(FB_W) -DFB_H=$(FB_H) \
	      -DARCH_$(ARCH)=1 -c $< -o $@

$(BUILD)/kernel/tls_roots.o: kernel/tls_roots.c | $(BUILD)/kernel
	$(CC) $(BEARSSL_CFLAGS) -Ikernel -c $< -o $@

# Static library so the kernel ELF doesn't bloat its symbol table.
$(BUILD)/libbearssl.a: $(BEARSSL_OBJS) $(SHIM_OBJ)
	@rm -f $@
	ar rcs $@ $(BEARSSL_OBJS) $(SHIM_OBJ)
	@echo "[OK] $@  ($$(wc -c < $@) bytes, $(words $(BEARSSL_OBJS)) BearSSL objs + shim)"

# ---- link kernel --------------------------------------------------------------
$(KERNEL): $(ASM_OBJS) $(C_OBJS) $(BUILD)/libbearssl.a linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS) $(BUILD)/libbearssl.a
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
run: run-disk

run-cdrom: $(ISO)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(KERNEL)
	$(QEMU) -kernel $< $(QEMU_FLAGS)

run-headless: run-disk-headless

# Sparse qcow2: host file grows as the guest writes; logical size $(DISK_CAPACITY).
$(BUILD)/falcon.img: | $(BUILD)
	@if [ ! -f $@ ]; then \
	  qemu-img create -f qcow2 $@ "$(DISK_CAPACITY)"; \
	  echo "[OK] new $@ (qcow2, capacity $(DISK_CAPACITY))"; \
	fi

run-disk: $(ISO) $(BUILD)/falcon.img
	$(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

run-disk-headless: $(ISO) $(BUILD)/falcon.img
	$(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(HEADLESS_FLAGS)

# Writes stay in QEMU’s overlay only — discarded when QEMU exits (“USB çıkarılınca kalmadan”).
run-disk-ephemeral: $(ISO) $(BUILD)/falcon.img
	$(QEMU) -snapshot -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

everything: iso run-disk

wipe-disk:
	rm -f $(BUILD)/falcon.img

font:
	python3 tools/genfont.py

$(BUILD) $(BUILD)/kernel $(BUILD)/boot $(BUILD)/linux:
	@mkdir -p $@

clean:
	rm -rf $(BUILD)
