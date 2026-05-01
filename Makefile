# =============================================================================
#  FalconOS — bare-metal build system
# -----------------------------------------------------------------------------
#  Targets:
#    all     build the kernel ELF (default)
#    iso     wrap kernel.elf into a bootable GRUB ISO
#    run     boot the ISO in QEMU
#    run-fb  boot kernel.elf directly via QEMU's -kernel  (faster iter)
#    font    regenerate kernel/font_data.c from DejaVu (requires Pillow)
#    clean   remove all build artefacts
# =============================================================================

CC          := gcc
LD          := ld
NASM        := nasm
QEMU        := qemu-system-i386

BUILD       := build
ISO_DIR     := $(BUILD)/iso

CFLAGS      := -m32 -ffreestanding -fno-pic -fno-stack-protector \
               -fno-builtin -nostdlib -nostdinc \
               -Wall -Wextra -Wno-unused-parameter \
               -O2 -Ikernel
LDFLAGS     := -m elf_i386 -T linker.ld -nostdlib

C_SRCS      := $(wildcard kernel/*.c)
C_OBJS      := $(C_SRCS:%.c=$(BUILD)/%.o)
ASM_OBJS    := $(BUILD)/boot/multiboot2.o

KERNEL      := $(BUILD)/falcon.elf
ISO         := $(BUILD)/FalconOS.iso

QEMU_FLAGS  := -m 64M -no-reboot -no-shutdown -serial stdio -display sdl
HEADLESS    := -m 64M -no-reboot -no-shutdown -serial stdio -display none -vga std

.PHONY: all iso run run-fb run-headless font clean

all: $(KERNEL)

# ---- compile C ---------------------------------------------------------------
$(BUILD)/kernel/%.o: kernel/%.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

# ---- assemble multiboot stub --------------------------------------------------
$(BUILD)/boot/multiboot2.o: boot/multiboot2.asm | $(BUILD)/boot
	$(NASM) -f elf32 $< -o $@

# ---- link kernel --------------------------------------------------------------
$(KERNEL): $(ASM_OBJS) $(C_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS)
	@echo "[OK] linked $@  ($$(wc -c < $@) bytes)"

# ---- ISO ----------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL)        $(ISO_DIR)/boot/falcon.elf
	cp boot/grub.cfg    $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null
	@echo "[OK] ISO   $@"

# ---- run ----------------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(KERNEL)
	$(QEMU) -kernel $< $(QEMU_FLAGS)

run-headless: $(ISO)
	$(QEMU) -cdrom $< $(HEADLESS)

# ---- font regeneration --------------------------------------------------------
font:
	python3 tools/genfont.py

# ---- helpers ------------------------------------------------------------------
$(BUILD) $(BUILD)/kernel $(BUILD)/boot:
	@mkdir -p $@

clean:
	rm -rf $(BUILD)
