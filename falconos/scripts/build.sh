#!/bin/bash
# FalconOS v2.1 "Nexus" - Build Script
# Builds the complete operating system

set -e

echo "=========================================="
echo "  FalconOS v2.1 'Nexus' Build System"
echo "=========================================="
echo ""

# Configuration
BUILD_TYPE=${BUILD_TYPE:-Release}
ARCH=$(uname -m)
BUILD_DIR="build"

echo "[*] Build Configuration:"
echo "    Architecture: $ARCH"
echo "    Build Type: $BUILD_TYPE"
echo "    Build Directory: $BUILD_DIR"
echo ""

# Check dependencies
echo "[*] Checking dependencies..."

check_command() {
    if ! command -v "$1" &> /dev/null; then
        echo "[!] Error: $1 is required but not installed."
        exit 1
    fi
}

check_command gcc
check_command rustc
check_command cmake
check_command make
check_command nasm

echo "[+] All dependencies found."
echo ""

# Create build directory
echo "[*] Creating build directory..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "[*] Configuring build with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
         -DCMAKE_INSTALL_PREFIX=/opt/falconos \
         -DARCH=$ARCH

# Build
echo "[*] Building FalconOS kernel..."
make -j$(nproc)

# Install
echo "[*] Installing FalconOS..."
make install DESTDIR=../dist

cd ..

# Create bootable ISO
echo "[*] Creating bootable ISO image..."
mkdir -p dist/iso/boot/grub

# Copy kernel to ISO structure
cp dist/opt/falconos/boot/falcon_kernel dist/iso/boot/

# Create GRUB configuration
cat > dist/iso/boot/grub/grub.cfg << 'EOF'
menuentry "FalconOS v2.1 Nexus" {
    set root=(hd0,msdos1)
    linux /boot/falcon_kernel quiet splash
    boot
}
EOF

# Generate ISO
if command -v grub-mkrescue &> /dev/null; then
    grub-mkrescue -o dist/falconos-v2.1-nexus.iso dist/iso
    echo "[+] ISO created: dist/falconos-v2.1-nexus.iso"
else
    echo "[!] Warning: grub-mkrescue not found. Skipping ISO creation."
fi

echo ""
echo "=========================================="
echo "  Build Complete!"
echo "=========================================="
echo ""
echo "Output files:"
echo "  - Kernel: dist/opt/falconos/boot/falcon_kernel"
echo "  - ISO: dist/falconos-v2.1-nexus.iso (if grub-mkrescue available)"
echo ""
echo "To test in QEMU:"
echo "  qemu-system-x86_64 -cdrom dist/falconos-v2.1-nexus.iso -m 2G"
echo ""
echo "To install on real hardware:"
echo "  sudo dd if=dist/falconos-v2.1-nexus.iso of=/dev/sdX bs=4M status=progress"
echo ""
