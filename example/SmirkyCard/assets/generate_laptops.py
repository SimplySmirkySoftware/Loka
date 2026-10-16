#!/usr/bin/env python3
# Original generated artwork, licensed under the same license as this repository.
"""Generate the LaptopTracker one-bit illustrations: five Apple portables.

NAME.PICT is the Classic input (data-fork PICT); NAME.PNG carries the same
bitmap for the Win32 and macOS rails, whose decoders do not read PICT. Every
picture is 160 by 100 pixels, drawn from geometry only (no traced artwork)."""

import importlib.util
from pathlib import Path

ENCODER_PATH = Path(__file__).resolve().parents[2] / "ScrapbookUI/assets/generate_picts.py"
SPEC = importlib.util.spec_from_file_location("scrapbook_picts", ENCODER_PATH)
ENCODER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENCODER)

WIDTH, HEIGHT = 160, 100
FRAME = (0, 0, HEIGHT, WIDTH)
ROW_BYTES = 20

# 8x8 patterns, one byte per row (1 = black).
SOLID = [0xFF] * 8
GRAY50 = [0xAA, 0x55] * 4
GRAY25 = [0x88, 0x00, 0x22, 0x00] * 2
GRAY12 = [0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00]
GRAY75 = [0x77, 0xFF, 0xDD, 0xFF] * 2
WHITE = [0x00] * 8


class Canvas:
    def __init__(self):
        self.bits = bytearray(HEIGHT * ROW_BYTES)

    def plot(self, x, y, black=True):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            mask = 1 << (7 - x % 8)
            index = y * ROW_BYTES + x // 8
            if black:
                self.bits[index] |= mask
            else:
                self.bits[index] &= ~mask & 0xFF

    def fill(self, x0, y0, x1, y1, pattern):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.plot(x, y, bool(pattern[y % 8] & (1 << (7 - x % 8))))

    def line(self, x0, y0, x1, y1):
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.plot(x0, y0)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def frame(self, x0, y0, x1, y1):
        self.line(x0, y0, x1 - 1, y0)
        self.line(x0, y1 - 1, x1 - 1, y1 - 1)
        self.line(x0, y0, x0, y1 - 1)
        self.line(x1 - 1, y0, x1 - 1, y1 - 1)

    def round_frame(self, x0, y0, x1, y1, r):
        self.line(x0 + r, y0, x1 - 1 - r, y0)
        self.line(x0 + r, y1 - 1, x1 - 1 - r, y1 - 1)
        self.line(x0, y0 + r, x0, y1 - 1 - r)
        self.line(x1 - 1, y0 + r, x1 - 1, y1 - 1 - r)
        for cx, cy, sx, sy in ((x0 + r, y0 + r, -1, -1), (x1 - 1 - r, y0 + r, 1, -1),
                               (x0 + r, y1 - 1 - r, -1, 1), (x1 - 1 - r, y1 - 1 - r, 1, 1)):
            self.arc(cx, cy, r, sx, sy)

    def arc(self, cx, cy, r, sx, sy):
        x, y, d = 0, r, 1 - r
        while x <= y:
            self.plot(cx + sx * x, cy + sy * y)
            self.plot(cx + sx * y, cy + sy * x)
            if d < 0:
                d += 2 * x + 3
            else:
                d += 2 * (x - y) + 5
                y -= 1
            x += 1

    def ellipse(self, cx, cy, rx, ry, pattern=None):
        for y in range(-ry, ry + 1):
            span = int(round(rx * (1 - (y / ry) ** 2) ** 0.5)) if ry else rx
            if pattern is not None:
                for x in range(-span + 1, span):
                    self.plot(cx + x, cy + y, bool(pattern[(cy + y) % 8] & (1 << (7 - (cx + x) % 8))))
            self.plot(cx - span, cy + y)
            self.plot(cx + span, cy + y)


def keyboard(c, x0, y0, x1, y1, rows, pitch):
    """Key caps as a grid of 1-px gaps over a light field."""
    c.fill(x0, y0, x1, y1, WHITE)
    c.frame(x0, y0, x1, y1)
    for r in range(1, rows):
        y = y0 + r * (y1 - y0) // rows
        c.line(x0, y, x1 - 1, y)
    x = x0 + pitch
    while x < x1 - 1:
        c.line(x, y0, x, y1 - 1)
        x += pitch


def clamshell(c, lid, screen, base, body, screen_pattern=WHITE, hinge=True):
    """lid/screen/base are (x0, y0, x1, y1) in pixels; body is the shell shade."""
    lx0, ly0, lx1, ly1 = lid
    bx0, by0, bx1, by1 = base
    # Base slab: filled shell with a 1-px outline, lid sits on it.
    c.fill(bx0, by0, bx1, by1, body)
    c.round_frame(bx0, by0, bx1, by1, 3)
    # Lid, slightly tilted back: draw as a rect with a darker bezel.
    c.fill(lx0, ly0, lx1, ly1, body)
    c.round_frame(lx0, ly0, lx1, ly1, 4)
    sx0, sy0, sx1, sy1 = screen
    c.fill(sx0, sy0, sx1, sy1, screen_pattern)
    c.frame(sx0, sy0, sx1, sy1)
    if hinge:
        c.line(lx0 + 6, ly1, lx1 - 7, ly1)
        c.line(lx0 + 6, ly1 + 1, lx1 - 7, ly1 + 1)


def desktop_icons(c, sx0, sy0, sx1, sy1):
    """A tiny menu bar and a window outline, so the screen reads as a Mac."""
    c.line(sx0 + 1, sy0 + 3, sx1 - 2, sy0 + 3)
    for x in range(sx0 + 3, sx0 + 15, 4):
        c.line(x, sy0 + 1, x + 1, sy0 + 1)
    wx0, wy0 = sx0 + (sx1 - sx0) // 5, sy0 + 7
    wx1, wy1 = sx1 - (sx1 - sx0) // 5, sy1 - 4
    if wx1 - wx0 > 8 and wy1 - wy0 > 8:
        c.frame(wx0, wy0, wx1, wy1)
        c.line(wx0, wy0 + 3, wx1 - 1, wy0 + 3)
        c.fill(wx0 + 1, wy0 + 1, wx1 - 1, wy0 + 3, GRAY50)


def trackball(c, cx, cy, r):
    c.ellipse(cx, cy, r + 3, r + 2, WHITE)
    c.ellipse(cx, cy, r, r, GRAY25)
    c.plot(cx - r // 2, cy - r // 2)


def trackpad(c, x0, y0, x1, y1):
    c.fill(x0, y0, x1, y1, GRAY12)
    c.round_frame(x0, y0, x1, y1, 2)
    c.line(x0 + 4, y1 + 3, x1 - 5, y1 + 3)
    c.frame(x0 + 2, y1 + 2, x1 - 2, y1 + 6)


def ground(c, x0, x1, y):
    c.line(x0, y, x1, y)
    for x in range(x0 + 2, x1, 6):
        c.plot(x, y + 2)


def macintosh_portable(c):
    """1989: a wide, deep slab with the keyboard on the left and the trackball
    on the right, carrying a smaller lid."""
    clamshell(c, lid=(28, 8, 132, 58), screen=(36, 14, 124, 50),
              base=(8, 58, 152, 92), body=GRAY12)
    desktop_icons(c, 36, 14, 124, 50)
    keyboard(c, 18, 66, 102, 86, 4, 7)
    trackball(c, 128, 76, 7)
    c.line(8, 62, 151, 62)  # the thick deck edge
    ground(c, 4, 156, 95)


def powerbook_100(c):
    """1991: compact, keyboard pushed back, the trackball centred in a palm rest."""
    clamshell(c, lid=(34, 10, 126, 60), screen=(42, 16, 118, 52),
              base=(26, 60, 134, 90), body=GRAY25)
    desktop_icons(c, 42, 16, 118, 52)
    keyboard(c, 34, 64, 126, 76, 3, 7)
    trackball(c, 80, 83, 4)
    ground(c, 4, 156, 95)


def powerbook_180c(c):
    """1993: the same body as the 100 series, with an active-matrix colour
    panel drawn as graded bands."""
    clamshell(c, lid=(28, 6, 132, 60), screen=(36, 12, 124, 52),
              base=(20, 60, 140, 90), body=GRAY25)
    bands = (GRAY75, GRAY50, GRAY25, GRAY12, WHITE)
    for i, pattern in enumerate(bands):
        c.fill(36 + 1, 12 + 1 + i * 8, 124 - 1, min(12 + 1 + (i + 1) * 8, 51), pattern)
    desktop_icons(c, 36, 12, 124, 52)
    keyboard(c, 30, 64, 130, 76, 3, 7)
    trackball(c, 80, 83, 4)
    ground(c, 4, 156, 95)


def powerbook_550c(c):
    """1994, Japan only: a black shell with a wider 10.4-inch panel and the
    trackball between two palm rests."""
    clamshell(c, lid=(22, 6, 138, 62), screen=(30, 12, 130, 54),
              base=(14, 62, 146, 92), body=GRAY75)
    c.fill(31, 13, 129, 53, GRAY12)
    desktop_icons(c, 30, 12, 130, 54)
    keyboard(c, 26, 66, 134, 78, 3, 7)
    trackball(c, 80, 85, 4)
    ground(c, 4, 156, 95)


def powerbook_5300c(c):
    """1995: the first PowerPC PowerBook; a trackpad replaces the trackball."""
    clamshell(c, lid=(26, 6, 134, 60), screen=(34, 12, 126, 52),
              base=(18, 60, 142, 92), body=GRAY25)
    c.fill(35, 13, 125, 51, GRAY12)
    desktop_icons(c, 34, 12, 126, 52)
    keyboard(c, 26, 64, 134, 76, 3, 7)
    trackpad(c, 68, 79, 92, 88)
    ground(c, 4, 156, 95)


MACHINES = (
    ("MACPORT", macintosh_portable),
    ("PB100", powerbook_100),
    ("PB180C", powerbook_180c),
    ("PB550C", powerbook_550c),
    ("PB5300C", powerbook_5300c),
)


def build_bitmap(draw):
    canvas = Canvas()
    draw(canvas)
    return canvas.bits


def build_picture(bitmap):
    operations = ENCODER.picture_preamble(FRAME)
    operations += ENCODER.padded_op(0x0098, ENCODER.pack_bits_rect(bitmap, FRAME))
    return ENCODER.finish_picture(FRAME, operations)


def build_png(bitmap):
    pixels = bytearray(WIDTH * HEIGHT * 3)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            black = bitmap[y * ROW_BYTES + x // 8] & (1 << (7 - x % 8))
            ENCODER.set_rgb(pixels, WIDTH, x, y, 0 if black else 255)
    return ENCODER.encode_rgb_png(WIDTH, HEIGHT, bytes(pixels))


if __name__ == "__main__":
    for name, draw in MACHINES:
        bitmap = build_bitmap(draw)
        for suffix, build in ((".PICT", build_picture), (".PNG", build_png)):
            output = Path(__file__).resolve().with_name(name + suffix)
            output.write_bytes(build(bitmap))
            print(f"{output.name}: {output.stat().st_size} bytes")
