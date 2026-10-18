#!/usr/bin/env python3
"""
Convert the PSPMania demo note skin into RGB565 sprite data for the
GB300 / SF2000 port.

The demo ships NoteSkins/dance/default with the authentic 36px arrows.
Only the "Down" sprites exist as textures; Left/Up/Right are declared in
.sprite/.actor files as BaseRotationZDegrees rotations of them:

    Left  ->   90 deg
    Up    ->  180 deg
    Right ->  270 deg   (-90)

So the four lane directions are derived here rather than shipped four times.

Output: src/sm_assets.c  (+ include/sm_assets.h)
"""

import os
import struct
import sys
import zlib

ARROW = 36              # ReadMe: "Arrow size is currently 36px"
LANES = 4
ROT_DEG = [90, 0, 180, 270]   # Left, Down, Up, Right

SRC = sys.argv[1] if len(sys.argv) > 1 else "."
OUT_C = sys.argv[2] if len(sys.argv) > 2 else "src/sm_assets.c"
OUT_H = sys.argv[3] if len(sys.argv) > 3 else "include/sm_assets.h"


# --------------------------------------------------------------------------
# minimal PNG reader (8-bit, palette or RGB/RGBA, non-interlaced)
# --------------------------------------------------------------------------
def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "not a png: " + path

    pos = 8
    idat = b""
    pal = None
    trns = None
    w = h = depth = ctype = None

    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, comp, filt, inter = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and inter == 0, "unsupported png %s" % path
        elif typ == b"PLTE":
            pal = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
        pos += 12 + ln

    raw = zlib.decompress(idat)

    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * nch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                c = prev[i - nch] if i >= nch else 0
                b = prev[i]
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    # expand to RGBA
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        if ctype == 3:
            idx = out[i]
            rgba[i * 4 + 0] = pal[idx * 3 + 0]
            rgba[i * 4 + 1] = pal[idx * 3 + 1]
            rgba[i * 4 + 2] = pal[idx * 3 + 2]
            rgba[i * 4 + 3] = trns[idx] if (trns and idx < len(trns)) else 255
        elif ctype == 0:
            v = out[i]; rgba[i*4:i*4+4] = bytes((v, v, v, 255))
        elif ctype == 4:
            v = out[i*2]; rgba[i*4:i*4+4] = bytes((v, v, v, out[i*2+1]))
        elif ctype == 2:
            rgba[i*4:i*4+3] = out[i*3:i*3+3]; rgba[i*4+3] = 255
        else:
            rgba[i*4:i*4+4] = out[i*4:i*4+4]

    return w, h, rgba


def to_rgb565(w, h, rgba):
    buf = []
    for i in range(w * h):
        a = rgba[i * 4 + 3]
        if a == 0:
            buf.append(0x0000)          # chroma key / fully transparent
            continue
        r = rgba[i*4] * a // 255
        g = rgba[i*4+1] * a // 255
        b = rgba[i*4+2] * a // 255
        buf.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return buf


def rot_cw(px, w, h):
    """Rotate 90 degrees clockwise: output is h x w."""
    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            out[x * h + (h - 1 - y)] = px[y * w + x]
    return out, h, w


def crop_frames(px, w, h, cols, rows):
    """Split a sprite sheet into cols*rows equally sized frames."""
    fw, fh = w // cols, h // rows
    frames = []
    for r in range(rows):
        for c in range(cols):
            f = []
            for y in range(fh):
                base = (r * fh + y) * w + c * fw
                f.extend(px[base:base + fw])
            frames.append((f, fw, fh))
    return frames


REGISTRY = []


def emit(name, frames, fh_dec, group=None):
    """Write a C array of `frames` (list of (list, w, h))."""
    if group is not None:
        REGISTRY.append((group, name, len(frames), frames[0][1], frames[0][2]))
    # One flat array: every frame of a sprite has the same pixel count, so the
    # descriptor table can index into it as `base + i * w * h`.
    lines = ["static const uint16_t %s[] = {" % name]
    fi = 0
    for f, w, h in frames:
        lines.append("/* frame %d */" % fi)
        for row in range(h):
            rowvals = ", ".join("0x%04X" % v for v in f[row * w:(row + 1) * w])
            lines.append("  " + rowvals + ("," if fi < len(frames) - 1 or row < h - 1 else ""))
        fi += 1
    lines.append("};")
    lines.append("#define %s_FRAMES %d" % (name.upper(), len(frames)))
    return "\n".join(lines)


def main():
    if not os.path.isdir(SRC):
        sys.exit("note skin dir not found: " + SRC)

    parts = []

    # ---- tap note: "Down Tap Note 4x4.png" = 4x4 grid of 36px frames -------
    # The 16 cells are all the same downward arrow at different points of its
    # fade cycle (NoteDisplay/TapNoteAnimationLengthInBeats=4), not four lane
    # directions.  Keeping one 4-frame cycle instead of all 16 quarters the
    # sprite table; the fade is the only visible difference between them.
    TAP_CYCLE = 4
    tap = os.path.join(SRC, "Down Tap Note 4x4.png")
    w, h, rgba = read_png(tap)
    px = to_rgb565(w, h, rgba)
    grid = crop_frames(px, w, h, 4, 4)
    fr = grid[:TAP_CYCLE]
    print("tap note %dx%d -> %dx%d grid, keeping %d-frame cycle of %dx%d"
          % (w, h, 4, 4, len(fr), fr[0][1], fr[0][2]))
    for lane in range(LANES):
        rot = []
        for f, fw, fh in fr:
            p, ww, hh = f, fw, fh
            for _ in range(ROT_DEG[lane] // 90):
                p, ww, hh = rot_cw(p, ww, hh)
            rot.append((p, ww, hh))
        parts.append(emit("sm_sprite_tap%d" % lane, rot, rot[0][1] * rot[0][2], "tap"))

    # ---- receptor waiting ----------------------------------------------------
    w, h, rgba = read_png(os.path.join(SRC, "Down Receptor Waiting.png"))
    px = to_rgb565(w, h, rgba)
    print("receptor waiting %dx%d" % (w, h))
    for lane in range(LANES):
        p, ww, hh = px, w, h
        for _ in range(ROT_DEG[lane] // 90):
            p, ww, hh = rot_cw(p, ww, hh)
        parts.append(emit("sm_sprite_receptor%d" % lane, [(p, ww, hh)], ww * hh, "receptor"))

    # ---- receptor lit (2 frames: bright flash then steady) -------------------
    w, h, rgba = read_png(os.path.join(SRC, "_Down Receptor Go 2x1.png"))
    px = to_rgb565(w, h, rgba)
    fr = crop_frames(px, w, h, 2, 1)
    print("receptor go %dx%d -> %d frames" % (w, h, len(fr)))
    for lane in range(LANES):
        rot = []
        for f, fw, fh in fr:
            p, ww, hh = f, fw, fh
            for _ in range(ROT_DEG[lane] // 90):
                p, ww, hh = rot_cw(p, ww, hh)
            rot.append((p, ww, hh))
        parts.append(emit("sm_sprite_reclit%d" % lane, rot, rot[0][1] * rot[0][2], "reclit"))

    # ---- hold pieces ---------------------------------------------------------
    for tag, fname in (("holdbody", "Down Hold Body Active.png"),
                       ("holdhead", "Down Hold Head Active.png"),
                       ("holdcap",  "Down Hold BottomCap Active.png")):
        w, h, rgba = read_png(os.path.join(SRC, fname))
        px = to_rgb565(w, h, rgba)
        print("%s %dx%d" % (tag, w, h))
        for lane in range(LANES):
            p, ww, hh = px, w, h
            for _ in range(ROT_DEG[lane] // 90):
                p, ww, hh = rot_cw(p, ww, hh)
            parts.append(emit("sm_sprite_%s%d" % (tag, lane),
                              [(p, ww, hh)], ww * hh, tag))

    # ---- hit explosion -------------------------------------------------------
    w, h, rgba = read_png(os.path.join(SRC, "Down Tap Explosion Bright.png"))
    px = to_rgb565(w, h, rgba)
    print("explosion %dx%d" % (w, h))
    for lane in range(LANES):
        p, ww, hh = px, w, h
        for _ in range(ROT_DEG[lane] // 90):
            p, ww, hh = rot_cw(p, ww, hh)
        parts.append(emit("sm_sprite_boom%d" % lane, [(p, ww, hh)], ww * hh, "boom"))

    # ---- descriptor tables ------------------------------------------------
    order = ["tap", "receptor", "reclit", "holdbody", "holdhead", "holdcap", "boom"]
    names = {"tap": "sm_spr_tap", "receptor": "sm_spr_receptor",
             "reclit": "sm_spr_reclit", "holdbody": "sm_spr_holdbody",
             "holdhead": "sm_spr_holdhead", "holdcap": "sm_spr_holdcap",
             "boom": "sm_spr_boom"}
    desc = []
    for g in order:
        lane_groups = [r for r in REGISTRY if r[0] == g]
        lane_groups.sort(key=lambda r: r[1])
        desc.append("const sm_sprite_t %s[4] = {" % names[g])
        for (gg, arr, nf, w, h) in lane_groups:
            desc.append("  { {")
            stride = w * h
            for i in range(8):
                desc.append("    %s," % (("%s + %d" % (arr, i * stride)) if i < nf else "0"))
            desc.append("  }, %d, %d, %d }," % (nf, w, h))
        desc.append("};")

    with open(OUT_C, "w") as f:
        f.write("/* Generated by tools/make_assets.py -- do not edit.\n")
        f.write(" * Source: PSPMania v4 demo, NoteSkins/dance/default (36px arrows).\n")
        f.write(" * Left/Up/Right are rotations of the shipped Down textures, which\n")
        f.write(" * is how the original .sprite/.actor files declare them. */\n")
        f.write('#include "sm_assets.h"\n\n')
        f.write("\n\n".join(parts))
        f.write("\n\n/* ---- descriptors ---- */\n")
        f.write("\n\n".join(desc))
        f.write("""

void sm_assets_init(void)
{
    /* Sprites are static RGB565 data generated from the demo note skin;
     * nothing to allocate at runtime. */
}
""")
        f.write("\n")
    print("wrote", OUT_C, os.path.getsize(OUT_C), "bytes")

    with open(OUT_H, "w") as f:
        f.write("#ifndef SM_ASSETS_H\n#define SM_ASSETS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* Generated by tools/make_assets.py -- do not edit. */\n")
        f.write("#define SM_ARROW_PX 36\n\n")
        f.write("""typedef struct {
    const uint16_t *frames[8];
    int            num_frames;
    int            w, h;
} sm_sprite_t;

extern const sm_sprite_t sm_spr_tap[4];
extern const sm_sprite_t sm_spr_receptor[4];
extern const sm_sprite_t sm_spr_reclit[4];
extern const sm_sprite_t sm_spr_holdbody[4];
extern const sm_sprite_t sm_spr_holdhead[4];
extern const sm_sprite_t sm_spr_holdcap[4];
extern const sm_sprite_t sm_spr_boom[4];

void sm_assets_init(void);

#endif /* SM_ASSETS_H */
""")
    print("wrote", OUT_H)


if __name__ == "__main__":
    main()