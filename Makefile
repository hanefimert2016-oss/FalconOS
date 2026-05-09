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
