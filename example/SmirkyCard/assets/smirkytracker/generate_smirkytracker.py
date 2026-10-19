#!/usr/bin/env python3
# Original hand-drawn artwork by the repository owner (Illustrator export),
# licensed under the same license as this repository.
"""Cut the SmirkyTracker sheet into per-machine pictures.

Input: the owner's Illustrator sheet (SmirkyTracker.png, RGB on white) and a
box list. Output per machine:
  NAME.PNG   320x200 color (Win32/macOS rails)
  NAME.PICT  320x200 8-bit indexed PICT v2 (Toolbox, Color QuickDraw)
  NAMEM.PNG  320x200 1-bit Bayer-dithered (gray-first downscale, then dither)
  NAMEM.PICT 320x200 1-bit PICT v2 (Toolbox, any QuickDraw)
  NAMEMS.PNG/.PICT 160x100 1-bit, dithered at that size: shrinking a 1-bit
             picture ORs its pixels and a dithered 320x200 turns black.
Mono is derived from the color art, never from a pre-thresholded sheet, so
thin strokes survive the downscale. Requires Pillow (development only)."""

import importlib.util, json, struct, sys
from pathlib import Path
from PIL import Image

HERE = Path(__file__).resolve().parent
ENCODER_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "../../../ScrapbookUI/assets/generate_picts.py"
SPEC = importlib.util.spec_from_file_location("scrapbook_picts", ENCODER_PATH)
ENC = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(ENC)
SHEET = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "SmirkyTracker.png"
OUT = Path(sys.argv[3]) if len(sys.argv) > 3 else HERE / ".."

SIZES = ((320, 200, 8, ""), (160, 100, 4, "S"))
W, H, PAD = 320, 200, 8
FRAME = (0, 0, H, W)
NAMES = ["NEO","MBP2023","MBP2019","MBP2015","MBP2011","MB2006","IBOOKG3",
         "TIBOOK","PISMO","PDQ","PB550C","PB180C","MACPLUS","ICON"]
BOXES = json.load(open(HERE / "boxes.json"))
BAYER = [[0,8,2,10],[12,4,14,6],[3,11,1,9],[15,7,13,5]]

def crop_fit(src, box, W=W, H=H, PAD=PAD):
    x0, y0, x1, y1 = box
    crop = src.crop((x0, y0, x1 + 1, y1 + 1))
    iw, ih = crop.size
    s = min((W - 2 * PAD) / iw, (H - 2 * PAD) / ih)
    nw, nh = max(1, round(iw * s)), max(1, round(ih * s))
    r = crop.resize((nw, nh), Image.BOX)
    canvas = Image.new(src.mode, (W, H), 255 if src.mode == "L" else (255, 255, 255))
    canvas.paste(r, ((W - nw) // 2, (H - nh) // 2))
    return canvas

def ordered_dither(gray):
    W, H = gray.size; rb = W // 8
    px = gray.load(); bits = bytearray(H * rb)
    for y in range(H):
        for x in range(W):
            if px[x, y] / 255.0 <= (BAYER[y % 4][x % 4] + 0.5) / 16.0:
                bits[y * rb + x // 8] |= 1 << (7 - x % 8)
    return bits

def mono_png(bits, W, H):
    rb = W // 8
    img = Image.new("1", (W, H), 1); px = img.load()
    for y in range(H):
        for x in range(W):
            if bits[y * rb + x // 8] & (1 << (7 - x % 8)): px[x, y] = 0
    return img

def mono_pict(bits, W, H):
    frame = (0, 0, H, W)
    ops = ENC.picture_preamble(frame)
    ops += ENC.padded_op(0x0098, ENC.pack_bits_rect(bits, frame))
    return ENC.finish_picture(frame, ops)

def color_pict(indexed):
    """PackBitsRect with an 8-bit indexed PixMap and its ColorTable."""
    pal = indexed.getpalette()[: 256 * 3]
    n = len(pal) // 3
    row_bytes = W  # 8 bits per pixel, already even
    pixmap = struct.pack(">H", row_bytes | 0x8000) + ENC.rect(FRAME)
    pixmap += struct.pack(">hhlllhhhhlll", 0, 0, 0, 72 << 16, 72 << 16,
                          0, 8, 1, 8, 0, 0, 0)
    ctab = struct.pack(">lhh", 0, 0, n - 1)
    for i in range(n):
        r, g, b = pal[3 * i: 3 * i + 3]
        ctab += struct.pack(">HHHH", i, r * 257, g * 257, b * 257)
    data = indexed.tobytes()
    rows = bytearray()
    for y in range(H):
        packed = ENC.pack_bits(data[y * W:(y + 1) * W])
        rows += struct.pack(">H", len(packed)) + packed  # rowBytes > 250: 2-byte counts
    payload = pixmap + ctab + ENC.rect(FRAME) + ENC.rect(FRAME) + struct.pack(">h", 0) + rows
    ops = ENC.picture_preamble(FRAME)
    ops += ENC.padded_op(0x0098, payload)
    return ENC.finish_picture(FRAME, ops)

def main():
    sheet = Image.open(SHEET).convert("RGB")
    for name, box in zip(NAMES, BOXES):
        color = crop_fit(sheet, box)
        color.save(OUT / f"{name}.PNG", optimize=True)
        indexed = color.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        (OUT / f"{name}.PICT").write_bytes(color_pict(indexed))
        for w, h, pad, suffix in SIZES:
            gray = crop_fit(sheet.convert("L"), box, w, h, pad)
            bits = ordered_dither(gray)
            mono_png(bits, w, h).save(OUT / f"{name}M{suffix}.PNG", optimize=True)
            (OUT / f"{name}M{suffix}.PICT").write_bytes(mono_pict(bits, w, h))
        print(name, len(indexed.getpalette()) // 3, "colors",
              (OUT / f"{name}.PICT").stat().st_size, (OUT / f"{name}M.PICT").stat().st_size)

if __name__ == "__main__":
    main()
