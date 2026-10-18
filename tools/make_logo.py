#!/usr/bin/env python3
"""
tools/make_logo.py

Converts StepMania logo PNG into RGB565 + Alpha header for GB300 / SF2000.
Target dimensions: 93x18 (fits comfortably in 304x22 header capsule).
"""

import sys
from PIL import Image

def main():
    src_png = sys.argv[1] if len(sys.argv) > 1 else "/tmp/stepmania_logo.png"
    out_h_file = sys.argv[2] if len(sys.argv) > 2 else "src/sm_logo_data.h"

    im = Image.open(src_png).convert("RGBA")
    target_h = 18
    target_w = int(im.size[0] * (target_h / im.size[1]))
    res = im.resize((target_w, target_h), Image.Resampling.LANCZOS)

    def to_565(r, g, b):
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

    rgb565 = []
    alpha = []

    for y in range(target_h):
        for x in range(target_w):
            r, g, b, a = res.getpixel((x, y))
            rgb565.append(to_565(r, g, b))
            alpha.append(a)

    with open(out_h_file, "w") as f:
        f.write("/* Generated StepMania logo data (RGB565 + Alpha) */\n")
        f.write("#ifndef SM_LOGO_DATA_H\n")
        f.write("#define SM_LOGO_DATA_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define SM_LOGO_W {target_w}\n")
        f.write(f"#define SM_LOGO_H {target_h}\n\n")

        f.write(f"static const uint16_t sm_logo_rgb565[{len(rgb565)}] = {{\n")
        for i in range(0, len(rgb565), 12):
            chunk = rgb565[i:i+12]
            f.write("    " + ", ".join(f"0x{c:04X}" for c in chunk) + ",\n")
        f.write("};\n\n")

        f.write(f"static const uint8_t sm_logo_alpha[{len(alpha)}] = {{\n")
        for i in range(0, len(alpha), 16):
            chunk = alpha[i:i+16]
            f.write("    " + ", ".join(f"0x{a:02X}" for a in chunk) + ",\n")
        f.write("};\n\n")
        f.write("#endif /* SM_LOGO_DATA_H */\n")

    print(f"Generated {out_h_file} ({target_w}x{target_h}) from {src_png}")

if __name__ == "__main__":
    main()
