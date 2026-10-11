#!/usr/bin/env bash
set -euo pipefail
: "${FALCON_ISO:=build/FalconOS.iso}"
: "${FALCON_OUT:=build/FalconOS-Native-Desktop-x86_64.AppImage}"
test -s "$FALCON_ISO"
mkdir -p build
APPDIR="$(mktemp -d)/FalconOS.AppDir"
mkdir -p "$APPDIR/usr/share/falconos"
cp "$FALCON_ISO" "$APPDIR/usr/share/falconos/FalconOS.iso"
cp tools/falcon_oneclick_launcher.py tools/falco_native_gateway.py \
   tools/falcon_https_gateway.py tools/make_safe_disk.py \
   tools/marketplace_bridge.py "$APPDIR/usr/share/falconos/"
cat >"$APPDIR/AppRun" <<'APP'
#!/bin/sh
HERE="$(cd "$(dirname "$0")" && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    if command -v zenity >/dev/null 2>&1; then
        zenity --error --title=FalconOS --text="Python 3 is required."
    fi
    exit 127
fi
exec python3 "$HERE/usr/share/falconos/falcon_oneclick_launcher.py" "$@"
APP
chmod 0755 "$APPDIR/AppRun"
cat >"$APPDIR/FalconOS.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=FalconOS Desktop
Comment=Start real FalconOS in a safe virtual machine
Exec=AppRun
Icon=falconos
Terminal=false
Categories=System;Emulator;
StartupNotify=true
DESKTOP
python3 - "$APPDIR/falconos.png" <<'PY'
from PIL import Image,ImageDraw,ImageFont
import sys
img=Image.new("RGBA",(256,256),(0,0,0,0))
d=ImageDraw.Draw(img)
d.rounded_rectangle((10,10,246,246),radius=62,fill=(17,47,99,255))
d.rounded_rectangle((22,22,234,234),radius=52,outline=(104,161,253,255),width=7)
try: f=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",148)
except OSError: f=ImageFont.load_default()
d.text((127,117),"F",fill="white",font=f,anchor="mm")
img.save(sys.argv[1])
PY
cp "$APPDIR/falconos.png" "$APPDIR/.DirIcon"
curl --fail --location --retry 3 --silent --show-error \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
    -o build/appimagetool-x86_64.AppImage
chmod +x build/appimagetool-x86_64.AppImage
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 build/appimagetool-x86_64.AppImage "$APPDIR" "$FALCON_OUT"
chmod +x "$FALCON_OUT"
test -s "$FALCON_OUT"
file "$FALCON_OUT"
sha256sum "$FALCON_OUT" > build/FalconOS-AppImage-SHA256.txt
echo "AppImage created: $FALCON_OUT"
