#!/usr/bin/env python3
# Original generated artwork, licensed under the same license as this repository.
"""Generate the deterministic 600-by-120 one-bit horizontal-scroll fixture."""

import importlib.util
from pathlib import Path


ENCODER_PATH = Path(__file__).resolve().parents[2] / "ScrapbookUI/assets/generate_picts.py"
SPEC = importlib.util.spec_from_file_location("scrapbook_picts", ENCODER_PATH)
ENCODER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENCODER)
FRAME = (0, 0, 120, 600)
ROW_BYTES = 76


def build_picture():
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
    operations = ENCODER.picture_preamble(FRAME)
    operations += ENCODER.padded_op(0x0098, ENCODER.pack_bits_rect(bitmap, FRAME))
    return ENCODER.finish_picture(FRAME, operations)


if __name__ == "__main__":
    output = Path(__file__).resolve().with_name("Wide.pict")
    output.write_bytes(build_picture())
    print(f"{output.name}: {output.stat().st_size} bytes")
