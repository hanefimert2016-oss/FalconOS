# =============================================================================
#  FalconOS — Build System
#  Targets:   all | run | clean | size
# =============================================================================

NASM        := nasm
QEMU        := qemu-system-i386
BUILD_DIR   := build
BOOT_SRC    := boot/boot.asm
BOOT_BIN    := $(BUILD_DIR)/boot.bin

# QEMU flags: no network, serial to stdio, boot from floppy image
QEMU_FLAGS  := -drive format=raw,file=$(BOOT_BIN),if=floppy \
               -boot a \
               -m 32M \
               -display sdl \
               -no-reboot \
               -no-shutdown

.PHONY: all run clean size

all: $(BOOT_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

## Assemble the bootloader to a flat 512-byte binary
$(BOOT_BIN): $(BOOT_SRC) | $(BUILD_DIR)
	$(NASM) -f bin $< -o $@
	@echo "[OK] Assembled: $@  ($(shell wc -c < $@) bytes)"

## Launch in QEMU
run: $(BOOT_BIN)
	$(QEMU) $(QEMU_FLAGS)

## Show binary size and confirm boot signature
size: $(BOOT_BIN)
	@echo "Size : $$(wc -c < $(BOOT_BIN)) bytes (must be 512)"
	@python3 -c " \
	    data = open('$(BOOT_BIN)', 'rb').read(); \
	    sig  = data[510:512]; \
	    ok   = '✓' if sig == b'\\x55\\xaa' else '✗'; \
	    print(f'Boot sig 0xAA55: {ok}  (bytes 510-511 = {sig.hex()})')"

clean:
	rm -rf $(BUILD_DIR)
