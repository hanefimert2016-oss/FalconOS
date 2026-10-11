#!/usr/bin/env python3
"""Generate FalconOS-native proportional grayscale type atlases at BUILD TIME.
No Chromium, WebKit or hosted HTML engine. The OS paints glyphs itself.
Text uses a local, already-installed OFL DejaVuSans/DejaVuSans-Bold asset.
The font file itself is NEVER redistributed; only baked alpha glyphs.
"""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
FONTS=[
 Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
 Path("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf")
]
FONT=next((p for p in FONTS if p.exists()),None)
if FONT is None:raise SystemExit("DejaVuSans-Bold.ttf missing")
CHARS=[chr(n) for n in range(32,127)]+list("ÇçĞğİıÖöŞşÜü")
assert len(CHARS)==107
SIZES=[(18,29,30),(36,53,55),(70,99,105)]
data=['/* Generated raster atlas for original FalconOS software renderer. */',
      '#include "falcon.h"','']
for size,w,h in SIZES:
    font=ImageFont.truetype(str(FONT),size)
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
