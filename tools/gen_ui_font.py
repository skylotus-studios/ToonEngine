#!/usr/bin/env python3
"""Bootstrap UI-font atlas generator for ToonEngine's in-game UI (roadmap #17).

CANONICAL PATH: the real UI atlas is meant to be baked with msdf-atlas-gen (MIT):

    msdf-atlas-gen -font assets/fonts/OpenSans-Medium.ttf -type mtsdf \\
        -size 48 -pxrange 4 -charset ascii.txt \\
        -imageout assets/fonts/ui/ui_font.png -json assets/fonts/ui/ui_font.json

This script is an INTERIM stand-in used only because msdf-atlas-gen is not installed here.
It renders glyphs with Pillow (which can only rasterize -- it cannot extract the vector
outlines a true multi-channel SDF needs) and computes a single-channel signed distance field
via a numpy Euclidean distance transform, then writes that SDF into all of R,G,B (+A). The
engine's median-of-3 shader (assets/shaders/ui.hlsl) reduces that to the same value, so it
renders identically to -- and is byte-compatible in schema with -- a real msdf-atlas-gen
"mtsdf" atlas. The only thing it lacks vs true MSDF is perfect sharp corners; swapping in a
real atlas is a pure asset replacement, no code change. Output schema matches msdf-atlas-gen's
-json exactly (atlas/metrics/glyphs, planeBounds in em units, atlasBounds in px, yOrigin
"bottom").

Usage:  python tools/gen_ui_font.py
"""
import json
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# --- Config -----------------------------------------------------------------
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_PATH = os.path.join(REPO, "assets", "fonts", "OpenSans-Medium.ttf")
OUT_DIR = os.path.join(REPO, "assets", "fonts", "ui")
OUT_PNG = os.path.join(OUT_DIR, "ui_font.png")
OUT_JSON = os.path.join(OUT_DIR, "ui_font.json")

EM = 48            # atlas em size in px (msdf-atlas-gen's "size")
SS = 4             # supersample factor for the rasterized mask
PXRANGE = 4.0      # SDF distance range in atlas px (msdf-atlas-gen's "distanceRange")
ATLAS_W = 512      # fixed atlas width; height grows to fit
CHARS = [chr(c) for c in range(32, 127)]  # printable ASCII

HI = EM * SS               # hi-res render size
PAD = int(round(PXRANGE * SS))  # padding around each glyph, hi-res px


# --- 1D squared Euclidean distance transform (Felzenszwalb & Huttenlocher) --
def _edt1d(f):
    n = f.shape[0]
    d = np.empty(n, dtype=np.float64)
    v = np.zeros(n, dtype=np.intp)
    z = np.empty(n + 1, dtype=np.float64)
    k = 0
    v[0] = 0
    z[0] = -1e20
    z[1] = 1e20
    for q in range(1, n):
        s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        while s <= z[k]:
            k -= 1
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        k += 1
        v[k] = q
        z[k] = s
        z[k + 1] = 1e20
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        d[q] = (q - v[k]) * (q - v[k]) + f[v[k]]
    return d


def edt(binary):
    """Euclidean distance (px) from every cell to the nearest True cell."""
    f = np.where(binary, 0.0, 1e20)
    for j in range(f.shape[1]):
        f[:, j] = _edt1d(f[:, j])
    for i in range(f.shape[0]):
        f[i, :] = _edt1d(f[i, :])
    return np.sqrt(f)


def downsample(a, ss):
    h, w = a.shape
    return a.reshape(h // ss, ss, w // ss, ss).mean(axis=(1, 3))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    font = ImageFont.truetype(FONT_PATH, HI)
    ascent, descent = font.getmetrics()  # px above / below baseline at size HI

    # Build each glyph's downsampled SDF cell (atlas px) + its em-space plane bounds.
    cells = []  # (unicode, advance_em, plane|None, sdf_uint8|None)
    for ch in CHARS:
        code = ord(ch)
        advance_em = font.getlength(ch) / HI
        box = font.getbbox(ch)  # (l, t, r, b), y=0 at top of line, baseline at ascent
        if box is None or box[2] <= box[0] or box[3] <= box[1]:
            cells.append((code, advance_em, None, None))  # e.g. space: advance only
            continue
        l, t, r, b = box
        ink_w, ink_h = r - l, b - t

        # Canvas = ink + PAD margins, rounded up so each side is divisible by SS.
        cw_hi = ink_w + 2 * PAD
        ch_hi = ink_h + 2 * PAD
        cw_hi += (-cw_hi) % SS
        ch_hi += (-ch_hi) % SS

        img = Image.new("L", (cw_hi, ch_hi), 0)
        ImageDraw.Draw(img).text((PAD - l, PAD - t), ch, font=font, fill=255)
        inside = np.asarray(img, dtype=np.uint8) > 127

        # Signed distance (hi-res px, + inside), downsampled and converted to atlas px.
        sd_hi = edt(~inside) - edt(inside)
        sd_px = downsample(sd_hi, SS) / SS
        val = np.clip(0.5 + sd_px / (2.0 * PXRANGE), 0.0, 1.0)
        sdf = np.round(val * 255.0).astype(np.uint8)

        # The cell covers glyph-x [l-PAD, l-PAD+cw_hi] and top-of-line-y [t-PAD, t-PAD+ch_hi];
        # convert to em, baseline-relative, y-up plane bounds.
        plane = {
            "left": (l - PAD) / HI,
            "bottom": (ascent - (t - PAD + ch_hi)) / HI,
            "right": (l - PAD + cw_hi) / HI,
            "top": (ascent - (t - PAD)) / HI,
        }
        cells.append((code, advance_em, plane, sdf))

    # Shelf-pack the cells into a fixed-width atlas (1px gutter).
    x = y = row_h = 0
    placed = []  # (code, advance, plane, sdf, gx, gy)
    for code, adv, plane, sdf in cells:
        if sdf is None:
            placed.append((code, adv, None, None, 0, 0))
            continue
        h, w = sdf.shape
        if x + w > ATLAS_W:
            x = 0
            y += row_h + 1
            row_h = 0
        placed.append((code, adv, plane, sdf, x, y))
        x += w + 1
        row_h = max(row_h, h)
    atlas_h = y + row_h
    atlas_h += (-atlas_h) % 4

    atlas = np.zeros((atlas_h, ATLAS_W), dtype=np.uint8)
    glyphs = []
    for code, adv, plane, sdf, gx, gy in placed:
        g = {"unicode": code, "advance": round(adv, 6)}
        if sdf is not None:
            h, w = sdf.shape
            atlas[gy:gy + h, gx:gx + w] = sdf
            g["planeBounds"] = {k: round(v, 6) for k, v in plane.items()}
            g["atlasBounds"] = {  # yOrigin "bottom": measured up from the atlas bottom
                "left": float(gx),
                "bottom": float(atlas_h - (gy + h)),
                "right": float(gx + w),
                "top": float(atlas_h - gy),
            }
        glyphs.append(g)

    # Replicate the SDF into RGBA (median(rgb) == the SDF) and save.
    rgba = np.dstack([atlas, atlas, atlas, atlas])
    Image.fromarray(rgba, "RGBA").save(OUT_PNG)

    meta = {
        "atlas": {
            "type": "mtsdf",  # median shader treats the replicated SDF as MSDF; real atlas drops in
            "distanceRange": PXRANGE,
            "distanceRangeMiddle": 0,
            "size": EM,
            "width": ATLAS_W,
            "height": int(atlas_h),
            "yOrigin": "bottom",
        },
        "metrics": {
            "emSize": 1,
            "lineHeight": (ascent + descent) / HI,
            "ascender": ascent / HI,
            "descender": -descent / HI,
            "underlineY": -0.1,
            "underlineThickness": 0.05,
        },
        "glyphs": glyphs,
        "kerning": [],
    }
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)

    # Sanity self-checks (no eyes on the atlas here). Sign check that doesn't depend on glyph
    # shape: the outer PAD ring of a glyph cell is always background (must read OUTSIDE, < 0.5),
    # while some stroke interior must read INSIDE (> 0.5). A flipped SDF fails the first assert.
    for tc in ("M", "H", "o"):
        _, _, _, sdf, _, _ = next(p for p in placed if p[0] == ord(tc))
        top_ring = sdf[0, :].max() / 255.0    # padding above the ink
        deepest = sdf.max() / 255.0           # deepest point inside a stroke
        assert top_ring < 0.4, f"'{tc}' padding ring should be outside (<0.4), got {top_ring:.3f}"
        assert deepest > 0.6, f"'{tc}' interior should be inside (>0.6), got {deepest:.3f}"

    print(f"wrote {OUT_PNG}  ({ATLAS_W}x{atlas_h}, {len(glyphs)} glyphs)")
    print(f"wrote {OUT_JSON}  (distanceRange={PXRANGE}, size={EM})")
    print("self-check ok: SDF sign correct (padding outside, stroke interiors inside)")


if __name__ == "__main__":
    main()
