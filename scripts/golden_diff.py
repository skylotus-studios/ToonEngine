#!/usr/bin/env python3
"""Perceptual diff between two PNGs captured by ToonPlayer --headless-render.

Companion to scripts/metrics_diff.py -- pure stdlib, no new dependency (this repo has no PIL,
no numpy, no requirements file anywhere; confirmed while writing this script). Since no image
library is available, this file includes its own minimal PNG reader: 8-bit RGB/RGBA,
non-interlaced, filter types 0-4 -- exactly what Renderer::CaptureFrameToPNG's
Diligent::Image::Encode call produces (core/rendering/renderer.cpp).

Metric: downsample both images to 1/4 resolution by 4x4 box-average (absorbs sub-pixel AA
jitter and PNG-encoder rounding noise), convert to luminance, take RMSE over the 0..1 range.
Cheap, dependency-free, and still sensitive to real structural change -- a missing shadow
cascade or a wrong sprite blend shifts whole regions, which survives 4x downsampling; per-pixel
AA/rounding noise doesn't. The post chain's stochastic effects (TAA, SSAO temporal, bloom, SSR)
are why golden captures are taken with `--headless-render --post off`, not something this
metric needs to be robust to on its own.
"""
import argparse
import struct
import sys
import zlib

DEFAULT_THRESHOLD = 0.02


def load_png(path):
    """Returns (width, height, rgba bytes, 4 bytes/pixel). Raises ValueError on anything this
    minimal reader doesn't support (interlaced, palette, <8-bit, non-PNG)."""
    with open(path, "rb") as f:
        data = f.read()

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")

    width = height = bit_depth = color_type = None
    idat = bytearray()
    pos = 8
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        chunk_type = data[pos + 4:pos + 8]
        chunk_data = data[pos + 8:pos + 8 + length]
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", chunk_data)
            if filter_method != 0:
                raise ValueError(f"{path}: unsupported PNG filter method {filter_method}")
            if interlace != 0:
                raise ValueError(f"{path}: interlaced PNG not supported")
            if bit_depth != 8:
                raise ValueError(f"{path}: only 8-bit PNGs are supported (got {bit_depth}-bit)")
            if color_type not in (2, 6):  # 2 = RGB, 6 = RGBA
                raise ValueError(f"{path}: only RGB/RGBA PNGs are supported (color type {color_type})")
        elif chunk_type == b"IDAT":
            idat += chunk_data
        elif chunk_type == b"IEND":
            break
        pos += 12 + length  # length + type + data + crc

    if width is None:
        raise ValueError(f"{path}: no IHDR chunk found")

    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels

    pixels = bytearray(width * height * 4)  # always expand to RGBA internally
    prev_row = bytearray(stride)
    src_off = 0
    for y in range(height):
        filter_type = raw[src_off]
        src_off += 1
        row = bytearray(raw[src_off:src_off + stride])
        src_off += stride
        _unfilter_row(row, prev_row, filter_type, channels)
        dst_off = y * width * 4
        if channels == 4:
            pixels[dst_off:dst_off + stride] = row
        else:
            for x in range(width):
                pixels[dst_off + x * 4:dst_off + x * 4 + 3] = row[x * 3:x * 3 + 3]
                pixels[dst_off + x * 4 + 3] = 255
        prev_row = row

    return width, height, bytes(pixels)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter_row(row, prev_row, filter_type, channels):
    """In-place PNG defilter (spec section 9.2), one scanline at a time."""
    length = len(row)
    if filter_type == 0:  # None
        return
    if filter_type == 1:  # Sub
        for i in range(channels, length):
            row[i] = (row[i] + row[i - channels]) & 0xFF
    elif filter_type == 2:  # Up
        for i in range(length):
            row[i] = (row[i] + prev_row[i]) & 0xFF
    elif filter_type == 3:  # Average
        for i in range(length):
            a = row[i - channels] if i >= channels else 0
            b = prev_row[i]
            row[i] = (row[i] + ((a + b) // 2)) & 0xFF
    elif filter_type == 4:  # Paeth
        for i in range(length):
            a = row[i - channels] if i >= channels else 0
            b = prev_row[i]
            c = prev_row[i - channels] if i >= channels else 0
            row[i] = (row[i] + _paeth(a, b, c)) & 0xFF
    else:
        raise ValueError(f"unsupported PNG filter type {filter_type}")


def _downsample_luminance(width, height, rgba, block=4):
    """Box-averages `block`x`block` pixel blocks to luminance (Rec. 601, ignoring alpha -- these
    captures are opaque back-buffer reads), producing a (dw, dh, list[float 0..1]) triple."""
    dw = max(1, width // block)
    dh = max(1, height // block)
    out = [0.0] * (dw * dh)
    for by in range(dh):
        for bx in range(dw):
            total = 0.0
            count = 0
            for y in range(by * block, min(by * block + block, height)):
                row_off = y * width * 4
                for x in range(bx * block, min(bx * block + block, width)):
                    off = row_off + x * 4
                    r, g, b = rgba[off], rgba[off + 1], rgba[off + 2]
                    total += (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
                    count += 1
            out[by * dw + bx] = total / count if count else 0.0
    return dw, dh, out


def perceptual_diff(path_a, path_b):
    """Returns the RMSE (0..1) between two PNGs' downsampled luminance. Raises ValueError on a
    dimension mismatch -- that's a scene/resolution change, not noise, and comparing anyway
    would silently produce a meaningless score."""
    wa, ha, pa = load_png(path_a)
    wb, hb, pb = load_png(path_b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch: {path_a} is {wa}x{ha}, {path_b} is {wb}x{hb}")

    _dw, _dh, la = _downsample_luminance(wa, ha, pa)
    _dw, _dh, lb = _downsample_luminance(wb, hb, pb)

    n = len(la)
    sq_sum = sum((x - y) * (x - y) for x, y in zip(la, lb))
    return (sq_sum / n) ** 0.5


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("current", help="freshly captured PNG")
    parser.add_argument("baseline", help="checked-in golden PNG (tests/golden/<scene>/frame.png)")
    parser.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD,
                        help=f"max allowed RMSE before this counts as a breach (default {DEFAULT_THRESHOLD})")
    args = parser.parse_args()

    try:
        score = perceptual_diff(args.current, args.baseline)
    except FileNotFoundError as e:
        print(f"golden_diff: {e.filename} not found -- if this is a new golden scene, run "
              f"scripts/rebaseline.py --reason \"...\" first", file=sys.stderr)
        return 1
    except ValueError as e:
        print(f"golden_diff: {e}", file=sys.stderr)
        return 1

    if score > args.threshold:
        print(f"golden_diff: FAIL score={score:.5f} threshold={args.threshold:.5f} "
              f"({args.current} vs {args.baseline})", file=sys.stderr)
        return 1

    print(f"golden_diff: OK score={score:.5f} threshold={args.threshold:.5f} "
          f"({args.current} vs {args.baseline})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
