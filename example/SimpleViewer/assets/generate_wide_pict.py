#!/usr/bin/env python3
# Original generated artwork, licensed under the same license as this repository.
"""Generate the deterministic 600-by-120 one-bit horizontal-scroll fixture.

Wide.pict is the Classic input (data-fork PICT); Wide.png carries the same
bitmap for the Win32 and macOS viewers, whose decoders do not read PICT."""

import importlib.util
from pathlib import Path


ENCODER_PATH = Path(__file__).resolve().parents[2] / "ScrapbookUI/assets/generate_picts.py"
SPEC = importlib.util.spec_from_file_location("scrapbook_picts", ENCODER_PATH)
ENCODER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENCODER)
FRAME = (0, 0, 120, 600)
ROW_BYTES = 76


def build_bitmap():
    bitmap = bytearray(FRAME[2] * ROW_BYTES)
    for y in range(FRAME[2]):
        for x in range(FRAME[3]):
            # Six different stripe periods keep the right edge distinguishable.
            black = (x // (2 + 2 * (x // 100))) % 2 == 0
            if 20 <= y < 100 and 190 <= x < 410:
                black = False
            if black:
                bitmap[y * ROW_BYTES + x // 8] |= 1 << (7 - x % 8)
    ENCODER.draw_glyph_line(bitmap, "WIDE", 9, (20, 190, 100, 410), ROW_BYTES)
    return bitmap


def build_picture():
    bitmap = build_bitmap()
    operations = ENCODER.picture_preamble(FRAME)
    operations += ENCODER.padded_op(0x0098, ENCODER.pack_bits_rect(bitmap, FRAME))
    return ENCODER.finish_picture(FRAME, operations)


def build_png():
    bitmap = build_bitmap()
    width, height = FRAME[3], FRAME[2]
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            black = bitmap[y * ROW_BYTES + x // 8] & (1 << (7 - x % 8))
            ENCODER.set_rgb(pixels, width, x, y, 0 if black else 255)
    return ENCODER.encode_rgb_png(width, height, bytes(pixels))


if __name__ == "__main__":
    for name, build in (("Wide.pict", build_picture), ("Wide.png", build_png)):
        output = Path(__file__).resolve().with_name(name)
        output.write_bytes(build())
        print(f"{output.name}: {output.stat().st_size} bytes")
