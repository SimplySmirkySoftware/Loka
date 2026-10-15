#!/usr/bin/env python3
"""Generate deterministic Scrapbook images for classic and modern packages."""

from pathlib import Path
import struct
import zlib


FRAME = (0, 0, 150, 200)
BADGE_FRAME = (0, 0, 16, 16)
PAGES = (
    ("1", "CHECKER", bytes.fromhex("aa55aa55aa55aa55")),
    ("2", "STRIPES", bytes.fromhex("f0f0f0f0f0f0f0f0")),
    ("3", "BLOCKS", bytes.fromhex("cc33cc33cc33cc33")),
    ("4", "DIAGONAL", bytes.fromhex("8040201008040201")),
)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_WIDTH = FRAME[3] - FRAME[1]
PNG_HEIGHT = FRAME[2] - FRAME[0]
PICT_WIDTH = FRAME[3] - FRAME[1]
PICT_HEIGHT = FRAME[2] - FRAME[0]
PICT_PIXEL_BYTES = (PICT_WIDTH + 7) // 8
PICT_ROW_BYTES = (PICT_PIXEL_BYTES + 1) & ~1
NUMBER_FIELD = (35, 65, 100, 135)
LABEL_FIELD = (105, 35, 138, 165)
PACK_BITS_RECT_OFFSET = 614

# Original, deliberately stylized five-by-seven glyphs for these generated
# pages. They extend the blocky DIGITS aesthetic used by the PNG pages; each
# entry pairs seven rows with its grid-cell advance.
PAGE_GLYPHS = {
    "1": ((0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110), 6),
    "2": ((0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111), 6),
    "3": ((0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110), 6),
    "4": ((0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010), 6),
    "A": ((0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001), 6),
    "B": ((0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110), 6),
    "C": ((0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111), 6),
    "D": ((0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110), 6),
    "E": ((0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111), 6),
    "G": ((0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110), 6),
    "H": ((0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001), 6),
    "I": ((0b11100, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b11100), 4),
    "K": ((0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001), 6),
    "L": ((0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111), 6),
    "N": ((0b10001, 0b11001, 0b11001, 0b10101, 0b10011, 0b10011, 0b10001), 6),
    "O": ((0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110), 6),
    "P": ((0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000), 6),
    "R": ((0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001), 6),
    "S": ((0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110), 6),
    "W": ((0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010), 6),
    "T": ((0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100), 6),
}


def word(value):
    return struct.pack(">H", value)


def rect(value):
    return struct.pack(">hhhh", *value)


def padded_op(opcode, payload=b""):
    encoded = word(opcode) + payload
    if len(encoded) & 1:
        encoded += b"\0"
    return encoded


def picture_preamble(frame):
    operations = bytearray()
    operations += word(0x0011)
    operations += word(0x02FF)
    operations += padded_op(
        0x0C00,
        struct.pack(
            ">hhllhhhhL",
            -2,
            0,
            72 << 16,
            72 << 16,
            *frame,
            0,
        ),
    )

    # An initial clip is not optional decoration: DrawPicture maps the
    # playback clip to the destination, and without a clip opcode the wide-open
    # default overflows 16-bit coordinates as soon as the picture is drawn
    # scaled, clipping every op to nothing. OpenPicture-recorded pictures get
    # this opcode from the recording port; a generated stream must carry it
    # itself. Region data: 2-byte region size (10) + bounding rect.
    operations += padded_op(0x0001, word(10) + rect(frame))  # Clip
    return operations


def finish_picture(frame, operations):
    operations += word(0x00FF)  # OpEndPic
    picture = word(10 + len(operations)) + rect(frame) + operations
    if len(picture) & 1:
        raise AssertionError("version 2 picture must remain word-aligned")
    return bytes(512) + picture


def png_chunk(kind, payload):
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def encode_rgb_png(width, height, pixels):
    if len(pixels) != width * height * 3:
        raise ValueError("RGB payload does not match the PNG dimensions")
    scanlines = bytearray()
    stride = width * 3
    for vertical in range(height):
        scanlines.append(0)
        start = vertical * stride
        scanlines.extend(pixels[start : start + stride])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        PNG_SIGNATURE
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
        + png_chunk(b"IEND", b"")
    )


def set_rgb(pixels, width, horizontal, vertical, value):
    offset = (vertical * width + horizontal) * 3
    pixels[offset : offset + 3] = bytes((value, value, value))


def build_png_page(number, pattern):
    pixels = bytearray(PNG_WIDTH * PNG_HEIGHT * 3)
    for vertical in range(PNG_HEIGHT):
        for horizontal in range(PNG_WIDTH):
            patterned = pattern[vertical % 8] & (1 << (7 - horizontal % 8))
            value = 45 if patterned else 225
            if horizontal in (0, PNG_WIDTH - 1) or vertical in (0, PNG_HEIGHT - 1):
                value = 0
            set_rgb(pixels, PNG_WIDTH, horizontal, vertical, value)

    # A generated five-by-seven digit keeps every page identifiable without
    # importing a font or third-party artwork into the repository.
    scale = 10
    glyph = PAGE_GLYPHS[number][0]
    glyph_left = (PNG_WIDTH - 5 * scale) // 2
    glyph_top = (PNG_HEIGHT - 7 * scale) // 2
    for vertical in range(glyph_top - 8, glyph_top + 7 * scale + 8):
        for horizontal in range(glyph_left - 8, glyph_left + 5 * scale + 8):
            set_rgb(pixels, PNG_WIDTH, horizontal, vertical, 255)
    for row, bits in enumerate(glyph):
        for column in range(5):
            if bits & (1 << (4 - column)):
                for dy in range(scale):
                    for dx in range(scale):
                        set_rgb(
                            pixels,
                            PNG_WIDTH,
                            glyph_left + column * scale + dx,
                            glyph_top + row * scale + dy,
                            0,
                        )
    return encode_rgb_png(PNG_WIDTH, PNG_HEIGHT, pixels)


def pack_bits(row):
    """Encode one scanline with Apple PackBits.

    This deliberately mirrors the development-only PICT mechanism in
    make_pmonsprite_lrp.py. Each generator keeps its decoder beside its encoder
    so its own output is checked without sharing the producer implementation.
    """
    packed = bytearray()
    cursor = 0
    while cursor < len(row):
        run = 1
        while (
            cursor + run < len(row)
            and row[cursor + run] == row[cursor]
            and run < 128
        ):
            run += 1
        if run >= 3:
            packed.append((1 - run) & 0xFF)
            packed.append(row[cursor])
            cursor += run
            continue

        literal_start = cursor
        cursor += run
        while cursor < len(row) and cursor - literal_start < 128:
            run = 1
            while (
                cursor + run < len(row)
                and row[cursor + run] == row[cursor]
                and run < 128
            ):
                run += 1
            if run >= 3:
                break
            cursor += run
        if cursor - literal_start > 128:
            cursor = literal_start + 128
        packed.append(cursor - literal_start - 1)
        packed += row[literal_start:cursor]
    return bytes(packed)


def unpack_pack_bits(packed):
    """Decode one scanline for the generation-time round-trip check."""
    row = bytearray()
    cursor = 0
    while cursor < len(packed):
        control = packed[cursor]
        cursor += 1
        if control <= 127:
            count = control + 1
            if cursor + count > len(packed):
                raise ValueError("truncated PackBits literal")
            row += packed[cursor : cursor + count]
            cursor += count
        elif control >= 129:
            count = 257 - control
            if cursor >= len(packed):
                raise ValueError("truncated PackBits repeat")
            row.extend([packed[cursor]] * count)
            cursor += 1
    return bytes(row)


def glyph_width(rows):
    rightmost = 0
    for bits in rows:
        for column in range(5):
            if bits & (1 << (4 - column)):
                rightmost = max(rightmost, column + 1)
    return rightmost


def line_width(text, scale):
    width = 0
    for index, character in enumerate(text):
        rows, advance = PAGE_GLYPHS[character]
        if index + 1 == len(text):
            width += glyph_width(rows) * scale
        else:
            width += advance * scale
    return width


def draw_glyph_line(bitmap, text, scale, field, row_bytes=PICT_ROW_BYTES):
    top, left, bottom, right = field
    horizontal = left + (right - left - line_width(text, scale)) // 2
    glyph_top = top + (bottom - top - 7 * scale) // 2
    for character in text:
        rows, advance = PAGE_GLYPHS[character]
        for row, bits in enumerate(rows):
            for column in range(5):
                if not bits & (1 << (4 - column)):
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        x = horizontal + column * scale + dx
                        y = glyph_top + row * scale + dy
                        bitmap[y * row_bytes + x // 8] |= 1 << (7 - x % 8)
        horizontal += advance * scale


def build_glyph_plane(number, label):
    bitmap = bytearray(PICT_HEIGHT * PICT_ROW_BYTES)
    draw_glyph_line(bitmap, number, 4, NUMBER_FIELD)
    draw_glyph_line(bitmap, label, 2, LABEL_FIELD)
    return bytes(bitmap)


def pack_bits_rect(bitmap, frame=FRAME):
    row_bytes = (((frame[3] - frame[1] + 7) // 8) + 1) & ~1
    packed_rows = bytearray()
    for row_start in range(0, len(bitmap), row_bytes):
        packed = pack_bits(bitmap[row_start : row_start + row_bytes])
        if len(packed) > 250:
            raise AssertionError("packed scanline exceeds a 1-byte length")
        packed_rows.append(len(packed))
        packed_rows += packed
    return (
        word(row_bytes)
        + rect(frame)
        + rect(frame)
        + rect(frame)
        + word(1)  # srcOr
        + packed_rows
    )


def check_prerasterized_picture(picture, bitmap):
    if picture[:512] != bytes(512):
        raise AssertionError("PICT file header must be 512 zero bytes")
    if struct.unpack(">H", picture[512:514])[0] != len(picture) - 512:
        raise AssertionError("PICT picSize does not match the encoded picture")
    if picture[514:522] != rect(FRAME):
        raise AssertionError("PICT picFrame is incorrect")
    if picture[522:526] != word(0x0011) + word(0x02FF):
        raise AssertionError("PICT version opcode is incorrect")
    if picture[552:564] != word(0x0001) + word(10) + rect(FRAME):
        raise AssertionError("PICT initial clip opcode is missing or malformed")

    expected_header = (
        word(0x0098)
        + word(PICT_ROW_BYTES)
        + rect(FRAME)
        + rect(FRAME)
        + rect(FRAME)
        + word(1)
    )
    header_end = PACK_BITS_RECT_OFFSET + len(expected_header)
    if picture[PACK_BITS_RECT_OFFSET:header_end] != expected_header:
        raise AssertionError("prerasterized PackBitsRect header is malformed")

    cursor = header_end
    for row_start in range(0, len(bitmap), PICT_ROW_BYTES):
        packed_length = picture[cursor]
        cursor += 1
        packed = picture[cursor : cursor + packed_length]
        cursor += packed_length
        expected = bitmap[row_start : row_start + PICT_ROW_BYTES]
        if expected[PICT_PIXEL_BYTES:] != bytes(PICT_ROW_BYTES - PICT_PIXEL_BYTES):
            raise AssertionError("PackBits scanline padding bits are not zero")
        if unpack_pack_bits(packed) != expected:
            raise AssertionError("PackBits scanline does not round-trip")
    if cursor & 1:
        if picture[cursor] != 0:
            raise AssertionError("PackBitsRect padding byte is not zero")
        cursor += 1
    if picture[cursor:] != word(0x00FF):
        raise AssertionError("PackBitsRect must be followed by OpEndPic")
    if len(picture) & 1:
        raise AssertionError("PICT file is not word-aligned")


def build_png_refused_badge():
    width = BADGE_FRAME[3] - BADGE_FRAME[1]
    height = BADGE_FRAME[2] - BADGE_FRAME[0]
    pixels = bytearray([255] * (width * height * 3))
    for vertical in range(height):
        for horizontal in range(width):
            dx = horizontal - (width - 1) / 2.0
            dy = vertical - (height - 1) / 2.0
            if dx * dx + dy * dy <= 7.0 * 7.0:
                set_rgb(pixels, width, horizontal, vertical, 0)
            if 3 <= horizontal <= 12 and (
                abs(horizontal - vertical) <= 1
                or abs((width - 1 - horizontal) - vertical) <= 1
            ):
                set_rgb(pixels, width, horizontal, vertical, 255)
    return encode_rgb_png(width, height, pixels)


def build_picture(number, label, pattern):
    # The identifying sequence for a version 2 picture, followed by the
    # extended header carrying 72 dpi resolution and the source rectangle.
    operations = picture_preamble(FRAME)

    operations += padded_op(0x000A, pattern)  # FillPat
    operations += padded_op(0x0034, rect((5, 5, 145, 195)))  # FillRect
    operations += padded_op(0x0030, rect(FRAME))  # FrameRect

    # Clear the two fields before compositing the generated black glyph plane.
    operations += padded_op(0x0032, rect(NUMBER_FIELD))  # EraseRect
    operations += padded_op(0x0032, rect(LABEL_FIELD))
    bitmap = build_glyph_plane(number, label)
    operations += padded_op(0x0098, pack_bits_rect(bitmap))  # PackBitsRect
    picture = finish_picture(FRAME, operations)
    check_prerasterized_picture(picture, bitmap)
    return picture


def build_refused_badge():
    operations = picture_preamble(BADGE_FRAME)
    operations += padded_op(0x0051, rect((1, 1, 15, 15)))  # PaintOval

    # Erasing paired 2x2 squares makes a bold white X without relying on a
    # font or copied artwork. The stepped edges stay distinct at 1-bit depth.
    for coordinate in range(4, 12, 2):
        operations += padded_op(
            0x0032,
            rect((coordinate, coordinate, coordinate + 2, coordinate + 2)),
        )
        operations += padded_op(
            0x0032,
            rect((coordinate, 14 - coordinate, coordinate + 2, 16 - coordinate)),
        )
    return finish_picture(BADGE_FRAME, operations)


def main():
    output_dir = Path(__file__).resolve().parent
    for index, (number, label, pattern) in enumerate(PAGES, start=1):
        payload = build_picture(number, label, pattern)
        (output_dir / f"page{index}.pict").write_bytes(payload)
        print(f"page{index}.pict: {len(payload)} bytes")
        png_payload = build_png_page(number, pattern)
        (output_dir / f"page{index}.png").write_bytes(png_payload)
        print(f"page{index}.png: {len(png_payload)} bytes")
    badge = build_refused_badge()
    (output_dir / "ngbadge.pict").write_bytes(badge)
    print(f"ngbadge.pict: {len(badge)} bytes")
    png_badge = build_png_refused_badge()
    (output_dir / "ngbadge.png").write_bytes(png_badge)
    print(f"ngbadge.png: {len(png_badge)} bytes")


if __name__ == "__main__":
    main()
