# =============================================================================
#  FalconOS — bare-metal build system  (FalconOS 1)
# -----------------------------------------------------------------------------
#  Targets:
#    all            build the kernel ELF (default)
#    start / everything  build ISO + QEMU with safe 4G raw demo disk (tek komut)
#    iso            wrap kernel.elf into a bootable GRUB ISO
#    run            same as run-disk (persistent raw test disk)
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

# BearSSL is opt-in: releases keep HTTPS FAIL-CLOSED until real TLS CI passes.
ENABLE_BEARSSL ?= 0
ENABLE_NATIVE_MARKET ?= 0
ifeq ($(ENABLE_NATIVE_MARKET),1)
ifneq ($(ENABLE_BEARSSL),1)
$(error Native Marketplace requires ENABLE_BEARSSL=1; insecure fetch is forbidden)
endif
NATIVE_MARKET_FLAGS := -DFALCON_NATIVE_MARKET
endif
TLS_CA_BUNDLE ?= /etc/ssl/certs/ca-certificates.crt
TLS_PATH := third_party/bearssl
ifeq ($(ENABLE_BEARSSL),1)
TLS_FLAGS := -DFALCON_BEARSSL -I$(TLS_PATH)/inc
TLS_OBJECTS := $(BUILD)/tls_roots.o
TLS_LIBRARY := $(TLS_PATH)/build/libbearssl.a
else
TLS_FLAGS :=
TLS_OBJECTS :=
TLS_LIBRARY :=
endif

ENABLE_RING3_TEST ?= 0
ifeq ($(ENABLE_RING3_TEST),1)
RING3_DEFS := -DFALCON_RING3_TEST
else
RING3_DEFS :=
endif

# Build-time baked glTF boot sprites are generated into build/ only.
# Use ENABLE_BOOT_GLB=1 after tools/bake_boot_intro.py is run.
ENABLE_BOOT_GLB ?= 1
ifeq ($(ENABLE_BOOT_GLB),1)
BOOT_GLB_FLAGS := -DFALCON_BOOT_GLB -I$(BUILD)
else
BOOT_GLB_FLAGS :=
endif

CFLAGS      := $(CFLAGS_ARCH) -std=gnu11 -ffreestanding -fno-pic -fno-stack-protector \
               -fno-builtin -nostdlib -nostdinc \
               -Wall -Wextra -Wno-unused-parameter \
               -O2 -Ikernel -Ilinux \
               -DFB_W=$(FB_W) -DFB_H=$(FB_H) -DARCH_$(ARCH)=1 $(EXTRA_CFLAGS) $(TLS_FLAGS) $(NATIVE_MARKET_FLAGS) $(RING3_DEFS) $(BOOT_GLB_FLAGS)
LDFLAGS     := $(LDFLAGS_ARCH) -T linker.ld -nostdlib -z noexecstack
NASMFLAGS   := -f $(NASMFMT) -DFB_W=$(FB_W) -DFB_H=$(FB_H) $(RING3_DEFS)

C_SRCS      := $(filter-out kernel/contour_font_data.c,$(wildcard kernel/*.c)) kernel/contour_font_data.c $(wildcard linux/*.c)
C_OBJS      := $(C_SRCS:%.c=$(BUILD)/%.o)
ASM_OBJS    := $(BUILD)/boot/multiboot2.o $(BUILD)/boot/isr.o

KERNEL      := $(BUILD)/falcon.elf
ISO         := $(BUILD)/FalconOS.iso
KERNEL_BIN  := $(BUILD)/FALCON-KERNEL.BIN
UEFI_EFI    := $(BUILD)/BOOTX64.EFI
UEFI_CLANG  ?= clang
UEFI_LINK   ?= lld-link


# Omit -no-shutdown / -no-reboot so ACPI power-off (PW_REG) and keyboard reset
# behave like real hardware and terminate or restart the QEMU process.
# SDL is the default. On Arch with QEMU GTK installed, use
# QEMU_DISPLAY=gtk,grab-on-hover=on when PS/2 mouse capture in SDL is awkward.
QEMU_DISPLAY  ?= sdl
RAM           ?= 12288
CPUS          ?= 6
VRAM          ?= 256
DISK_CAPACITY ?= 4G
OVMF_CODE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd))
OVMF_VARS_TEMPLATE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd))
OVMF_VARS := $(BUILD)/FalconBoot-OVMF-VARS.fd
UEFI_QEMU_FLAGS := -machine q35 \
    -drive if=pflash,format=raw,unit=0,readonly=on,file=$(OVMF_CODE) \
    -drive if=pflash,format=raw,unit=1,file=$(OVMF_VARS)


# Boot the FalconBoot UEFI CD-ROM before the unbootable test data disk.
# QEMU now also requires OVMF x86_64 UEFI firmware (not SeaBIOS).
QEMU_FLAGS    := $(UEFI_QEMU_FLAGS) -m $(RAM)M -smp $(CPUS) -boot order=d \
                 -serial unix:$(CURDIR)/$(BUILD)/falcon-market.sock,server=on,wait=off \
                 -netdev user,id=net0 -device rtl8139,netdev=net0 \
                 -display $(QEMU_DISPLAY) -vga std -global VGA.vgamem_mb=$(VRAM) \
                 -accel kvm -accel tcg

HEADLESS_FLAGS:= $(UEFI_QEMU_FLAGS) -m $(RAM)M -smp $(CPUS) -boot order=d \
                 -serial unix:$(CURDIR)/$(BUILD)/falcon-market.sock,server=on,wait=off \
                 -netdev user,id=net0 -device rtl8139,netdev=net0 \
                 -display none -vga std -global VGA.vgamem_mb=$(VRAM) \
                 -accel kvm -accel tcg

.PHONY: start all iso everything run run-cdrom run-fb run-headless \
        run-disk run-disk-headless run-disk-ephemeral wipe-disk font clean

# Türkçe README’de de geçecek tek komut: ISO derle + QEMU (kalıcı qcow2 disk).
start: everything

all: $(KERNEL)

RUN_DISK_DRIVE := file=$(BUILD)/falcon-safe.raw,format=raw,if=ide,index=0

# ---- compile C ---------------------------------------------------------------

# Baked FALCO owned native proportional fonts; Pillow is a build tool only.
kernel/contour_font_data.c: tools/gen_contour_font.py
	python3 tools/gen_contour_font.py

$(BUILD)/kernel/%.o: kernel/%.c kernel/falcon.h | $(BUILD)/kernel
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/linux/%.o: linux/%.c kernel/falcon.h | $(BUILD)/linux
	$(CC) $(CFLAGS) -c $< -o $@

# ---- assemble nasm sources ----------------------------------------------------
$(BUILD)/boot/%.o: boot/%.asm | $(BUILD)/boot
	$(NASM) $(NASMFLAGS) $< -o $@

# BearSSL TLS library (fixed upstream SHA-256 and audited host PEM roots).
ifeq ($(ENABLE_BEARSSL),1)
$(TLS_LIBRARY):
	python3 tools/bootstrap_bearssl.py
	$(MAKE) -C $(TLS_PATH) build/libbearssl.a CC=gcc \
	  "CFLAGS=-O2 -std=c99 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -m64 -mcmodel=kernel -mno-red-zone -mno-sse -mno-sse2 -Iinc -Isrc"

$(BUILD)/tls_roots.c: $(TLS_CA_BUNDLE) tools/generate_tls_roots.py | $(BUILD)
	python3 tools/generate_tls_roots.py --bundle $(TLS_CA_BUNDLE) --output $@

$(BUILD)/tls_roots.o: $(BUILD)/tls_roots.c $(TLS_LIBRARY)
	$(CC) $(filter-out -nostdinc,$(CFLAGS)) -c $< -o $@

# BearSSL header files depend on standard integer types, unlike our kernel.
$(BUILD)/kernel/https_bearssl.o: kernel/https_bearssl.c kernel/falcon.h $(TLS_LIBRARY) | $(BUILD)/kernel
	$(CC) $(filter-out -nostdinc,$(CFLAGS)) -c $< -o $@
endif

# ---- original user-provided boot intro -----------------------------------------
# Bake real 3D keyframes from the uploaded GLB at build time; OS itself has
# only a bounded, freestanding RGB sprite player. Use a private venv when the
# host lacks Python 3D dependencies: never modify the host's Python install.
ifeq ($(ENABLE_BOOT_GLB),1)
$(BUILD)/boot_model_frames.inc: assets/boot/falconos_boot_intro_animation.glb tools/bake_boot_intro.py | $(BUILD)
	@if python3 -c 'import numpy, cv2, trimesh, PIL' >/dev/null 2>&1; then \
	  python3 tools/bake_boot_intro.py --source $< --out $@ --frames 48; \
	else \
	  python3 -m venv $(BUILD)/boot-intro-venv && \
	  $(BUILD)/boot-intro-venv/bin/pip install --disable-pip-version-check 'numpy<3' trimesh opencv-python-headless pillow && \
	  $(BUILD)/boot-intro-venv/bin/python tools/bake_boot_intro.py --source $< --out $@ --frames 48; \
	fi

$(BUILD)/kernel/boot_glb_animation.o: $(BUILD)/boot_model_frames.inc
$(BUILD)/kernel/main.o: $(BUILD)/boot_model_frames.inc
endif

# ---- link kernel --------------------------------------------------------------

$(KERNEL): $(ASM_OBJS) $(C_OBJS) $(TLS_OBJECTS) $(TLS_LIBRARY) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ASM_OBJS) $(C_OBJS) $(TLS_OBJECTS) $(TLS_LIBRARY)
	@echo "[OK] linked $@  ($$(wc -c < $@) bytes, ARCH=$(ARCH), back-buffer $(FB_W)×$(FB_H))"

# ---- GRUB-FREE FalconBoot/1 x86_64 UEFI ISO -----------------------------------
# The EFI FAT filesystem holds our own /EFI/BOOT/BOOTX64.EFI and a flat
# fixed-address 32 MiB kernel binary. There is no GRUB in the boot chain.
iso: $(ISO)

$(KERNEL_BIN): $(KERNEL)
	objcopy -O binary $(KERNEL) $@
	@test $$(wc -c < $@) -lt 67108864 || { echo "Kernel exceeds FalconBoot reserved 64MiB"; exit 1; }

$(UEFI_EFI): $(KERNEL_BIN) boot/falcon_uefi/loader.c boot/falcon_uefi/uefi.h boot/falcon_uefi/handoff.S
	$(UEFI_CLANG) -target x86_64-pc-windows-msvc -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -mno-red-zone -Wall -Wextra \
	  -DFALCON_KERNEL_BYTES=$$(wc -c < $(KERNEL_BIN)) \
	  -DFALCON_KERNEL_ENTRY=0x$$(nm -n $(KERNEL) | awk '$$3=="long_start"{print $$1;exit}')ULL \
	  -c boot/falcon_uefi/loader.c -o $(BUILD)/falcon-loader.obj
	$(UEFI_CLANG) -target x86_64-pc-windows-msvc -c boot/falcon_uefi/handoff.S -o $(BUILD)/falcon-handoff.obj
	$(UEFI_LINK) /subsystem:efi_application /entry:EfiMain /nodefaultlib \
	  /out:$(UEFI_EFI) $(BUILD)/falcon-loader.obj $(BUILD)/falcon-handoff.obj
	@echo "[OK] FalconBoot UEFI application $@"

$(ISO): $(UEFI_EFI) $(KERNEL_BIN)
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)
	@truncate -s 25165824 $(ISO_DIR)/falcon-esp.img
	mformat -i $(ISO_DIR)/falcon-esp.img -h 64 -t 48 -s 16 -v FALCONV2 ::
	mmd -i $(ISO_DIR)/falcon-esp.img ::/EFI ::/EFI/BOOT ::/FALCONOS
	mcopy -i $(ISO_DIR)/falcon-esp.img $(UEFI_EFI) ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(ISO_DIR)/falcon-esp.img $(KERNEL_BIN) ::/FALCONOS/KERNEL.BIN
	xorriso -as mkisofs -r -J -V FALCONOS_V2 -e falcon-esp.img -no-emul-boot -o $(ISO) $(ISO_DIR)
	@echo "[OK] GRUB-free FalconOS V2 UEFI ISO $@ (native FalconBoot/1)"

# OVMF nonvolatile variables are copied into build/; never alter the
# system firmware image. Required for a genuine x86_64 UEFI boot session.
$(OVMF_VARS): | $(BUILD)
	@test -f "$(OVMF_CODE)" && test -f "$(OVMF_VARS_TEMPLATE)" || { echo "Missing OVMF: install ovmf"; exit 1; }
	cp $(OVMF_VARS_TEMPLATE) $@

# ---- run ----------------------------------------------------------------------
# Run FalconOS first, then start this in a second terminal to enable
# GitHub Releases downloads through the opt-in COM1 bridge.
.PHONY: market-bridge market-bridge-publish run-market run-market-publish run-market-publish-vm
market-bridge:
	python3 tools/marketplace_bridge.py --socket $(BUILD)/falcon-market.sock

# Host-only upload: reads an existing .app.pkg and creates a public Release.
# No kernel build, NASM, QEMU, ISO or VM is required.
# Usage: make run-market-publish PKG=/path/to/your.app.pkg
#        make run-market-publish  # prompts for a file path and confirmation
run-market-publish:
	@python3 tools/publish_app.py

# Separate, opt-in bridge mode for publishing from Codedium / Discover
# *inside a running FalconOS guest*. Kept for existing VM workflows.
market-bridge-publish:
	python3 tools/marketplace_bridge.py --socket $(BUILD)/falcon-market.sock --enable-publish

# Start both authenticated host bridges automatically: GitHub Releases
# over COM1 and read-only, certificate-verified HTTPS for Falco search/pages.
# Bind the HTTPS gateway only to loopback for QEMU user-mode networking.
# Neither requires a GitHub token for downloads.
run-market-publish-vm: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	@python3 tools/marketplace_bridge.py --socket $(BUILD)/falcon-market.sock --enable-publish & \
	  bridge_pid=$!; \
	  python3 tools/falcon_https_gateway.py --bind 127.0.0.1 & \
	  web_pid=$!; \
	  trap 'kill $bridge_pid $web_pid 2>/dev/null || true' EXIT; \
	  $(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

# Starts QEMU + both HTTPS-backed services, and cleans them on exit.
run-market: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	@python3 tools/marketplace_bridge.py --socket $(BUILD)/falcon-market.sock & \
	  bridge_pid=$!; \
	  python3 tools/falcon_https_gateway.py --bind 127.0.0.1 & \
	  web_pid=$!; \
	  trap 'kill $bridge_pid $web_pid 2>/dev/null || true' EXIT; \
	  $(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

# Run the completely native-rendered Falco (no Chromium, no browser-pixel bridge).
# Guest uses own layout engine; Python stdlib host is only for verified HTTPS.
.PHONY: run-falco-native
run-falco-native: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	@python3 tools/marketplace_bridge.py --socket $(BUILD)/falcon-market.sock & \
	  market_pid=$!; \
	  python3 tools/falcon_https_gateway.py --bind 127.0.0.1 & \
	  https_pid=$!; \
	  python3 tools/falco_native_gateway.py --bind 127.0.0.1 & \
	  native_pid=$!; \
	  trap 'kill $market_pid $https_pid $native_pid 2>/dev/null || true' EXIT; \
	  $(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

run: run-disk

run-cdrom: $(ISO) $(OVMF_VARS)
	$(QEMU) -cdrom $< $(QEMU_FLAGS)

run-fb: $(ISO) $(OVMF_VARS)
	@echo "FalconBoot is UEFI-only; booting the ISO (not legacy -kernel)"
	$(QEMU) -cdrom $(ISO) $(QEMU_FLAGS)

run-headless: run-disk-headless

# Create a dedicated, sparse MBR-partitioned QEMU test image (never touch legacy qcow2).
$(BUILD)/falcon-safe.raw: | $(BUILD)
	python3 tools/make_safe_disk.py --image $@ --size $(DISK_CAPACITY)

run-disk: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	$(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

run-disk-headless: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	$(QEMU) -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(HEADLESS_FLAGS)

# Writes stay in QEMU’s overlay only — discarded when QEMU exits (“USB çıkarılınca kalmadan”).
run-disk-ephemeral: $(ISO) $(BUILD)/falcon-safe.raw $(OVMF_VARS)
	$(QEMU) -snapshot -cdrom $(ISO) -drive $(RUN_DISK_DRIVE) $(QEMU_FLAGS)

everything: iso run-disk

wipe-disk:
	rm -f $(BUILD)/falcon-safe.raw

font:
	python3 tools/genfont.py

$(BUILD) $(BUILD)/kernel $(BUILD)/boot $(BUILD)/linux:
	@mkdir -p $@

clean:
	@rm -rf $(BUILD)/kernel $(BUILD)/linux $(BUILD)/boot $(ISO_DIR)
	@rm -f $(KERNEL) $(ISO) $(KERNEL_BIN) $(UEFI_EFI) $(BUILD)/falcon-*.obj $(OVMF_VARS)
	@echo "[OK] Build outputs cleaned; persistent disk images preserved"
