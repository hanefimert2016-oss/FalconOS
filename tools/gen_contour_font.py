#!/usr/bin/env python3
"""Generate FalconOS-native proportional grayscale type atlases at BUILD TIME.
No Chromium, WebKit or hosted HTML engine. The OS paints glyphs itself.
Text uses a local, already-installed OFL DejaVuSans/DejaVuSans-Bold asset.
The font file itself is NEVER redistributed; only baked alpha glyphs.
"""
from pathlib import Path
import hashlib
import urllib.request
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
FONTS=[
 Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
 Path("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf")
]
FONT=next((p for p in FONTS if p.exists()),None)
if FONT is None:raise SystemExit("DejaVuSans-Bold.ttf missing")
# The LIVE Contour page actually specifies Manrope as its display family.
# Bake a license-compatible, pinned revision at BUILD TIME, not a browser
# font loader. The source font file does not ship in the AppImage/ISO.
MANROPE_URL=("https://raw.githubusercontent.com/google/fonts/"
             "8f9a401dbb3793e0d1264b15d96aa253f05280f5/"
             "ofl/manrope/Manrope%5Bwght%5D.ttf")
MANROPE_SHA="d0639be45d0af36e798172419d7bd173c4bd4f29e2b76cbb69db1d11bf8b0a40"
try:
    req=urllib.request.Request(MANROPE_URL,headers={"User-Agent":"FalconOS-FontBake/1.0"})
    with urllib.request.urlopen(req,timeout=18) as response:
        font_bytes=response.read(200000)
    if hashlib.sha256(font_bytes).hexdigest()!=MANROPE_SHA:
        raise ValueError("Untrusted upstream font bytes: checksum mismatch")
    target=ROOT/"build"/"contour-source-manrope.ttf"
    target.parent.mkdir(exist_ok=True,parents=True)
    target.write_bytes(font_bytes)
    FONT=target
    print("CONTOUR_FONT: pinned Manrope SIL OFL source verified")
except Exception as e:
    print("CONTOUR_FONT: offline fallback to local DejaVu Sans:",repr(e))
CHARS=[chr(n) for n in range(32,127)]+list("ÇçĞğİıÖöŞşÜü")
assert len(CHARS)==107
SIZES=[(18,29,30),(36,53,55),(70,99,105)]
data=['/* Generated raster atlas for original FalconOS software renderer. */',
      '#include "falcon.h"','']
for size,w,h in SIZES:
    font=ImageFont.truetype(str(FONT),size)
    if FONT.name=="contour-source-manrope.ttf":
        try:font.set_variation_by_axes([800 if size>40 else (700 if size>24 else 600)])
        except (AttributeError,ValueError,OSError):pass
    data.append(f"const u8 FC_FONT_{size}[107][{w*h}] = {{")
    advances=[]
    for ch in CHARS:
        im=Image.new("L",(w,h),0)
        ImageDraw.Draw(im).text((2,3),ch,fill=255,font=font,anchor="lt")
        advance=min(w,int(font.getlength(ch)+.5)+1)
        advances.append(advance)
        row=im.tobytes()
        # 1 C array per codepoint; reject interpolated font graphics.
        data.append("{"+",".join(str(c) for c in row)+"},")
    data.append("};")
    data.append(f"const u8 FC_ADV_{size}[107]={{"+",".join(str(k) for k in advances)+"};")
(ROOT/"kernel/contour_font_data.c").write_text("\n".join(data)+"\n")
print("Native antialiased proportional font glyphs built:",[(s,w,h) for s,w,h in SIZES])
