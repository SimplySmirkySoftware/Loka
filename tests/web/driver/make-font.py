#!/usr/bin/env python3
"""Original CC0 test font: rectangular 'a'/'x', language-dependent advance.
No third-party outlines or font data. Standard-library-only SFNT construction.
Writes only below build/. This is a test asset, never a shipping dependency.
"""
from pathlib import Path
import struct
import sys


def u16(*values):
    return struct.pack('>' + 'H' * len(values), *values)


def s16(*values):
    return struct.pack('>' + 'h' * len(values), *values)


def u32(*values):
    return struct.pack('>' + 'I' * len(values), *values)


def checksum(data):
    data += b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>' + 'I' * (len(data) // 4), data)) & 0xffffffff


def font():
    # Five identical rectangle outlines; hmtx alone discriminates language.
    glyph = s16(1, 0, 0, 400, 700) + u16(3, 0) + bytes([1]*4)
    glyph += s16(0, 400, 0, -400) + s16(0, 0, 700, 0)
    glyph += b'\0' * (-len(glyph) % 4)
    tables = {}
    tables['glyf'] = glyph * 5
    tables['loca'] = u32(*(len(glyph)*i for i in range(6)))
    tables['hmtx'] = b''.join(u16(w, 0) for w in [500, 500, 600, 700, 800])
    tables['head'] = (u32(0x10000, 0x10000, 0, 0x5f0f3cf5) + u16(3, 1000)
                      + b'\0'*16 + s16(0, 0, 400, 700) + u16(0, 8) + s16(2, 1, 0))
    tables['hhea'] = (u32(0x10000) + s16(800, -200, 0) + u16(800)
                      + s16(0, 100, 400, 1, 0, 0) + b'\0'*8 + s16(0) + u16(5))
    tables['maxp'] = u32(0x10000) + u16(5, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    # A/V kerning is a controlled range-measurement discriminator; x is narrow.
    mappings = [(65, 1), (86, 2), (97, 1), (120, 3), (65535, 0)]
    ends = [cp for cp, _ in mappings]
    cmap = u16(4, 16+8*len(mappings), 0, 2*len(mappings), 8, 2, 2)
    cmap += u16(*ends) + u16(0) + u16(*ends)
    cmap += u16(*((glyph-cp)&65535 for cp, glyph in mappings)) + u16(*([0]*len(mappings)))
    tables['cmap'] = u16(0, 1, 3, 1) + u32(12) + cmap
    tables['hmtx'] = b''.join(u16(w, 0) for w in [500, 500, 600, 200, 800])
    # Microsoft/OpenType legacy horizontal kern format 0: glyph A(1), V(2).
    tables['kern'] = u16(0, 1, 0, 20, 1, 1, 6, 0, 0, 1, 2) + s16(-300)
    names = [(1, 'Loka Text Probe'), (2, 'Regular'), (4, 'Loka Text Probe Regular'),
             (6, 'LokaTextProbe-Regular'), (0, 'CC0; original Loka test font')]
    strings = b''; records = b''
    for name_id, value in names:
        data = value.encode('utf-16-be')
        records += u16(3, 1, 0x409, name_id, len(data), len(strings))
        strings += data
    tables['name'] = u16(0, len(names), 6 + 12*len(names)) + records + strings
    tables['post'] = u32(0x30000, 0) + s16(-100, 50) + u32(0, 0, 0, 0, 0)
    tables['OS/2'] = (u16(0) + s16(500) + u16(400, 5, 0)
                     + s16(650, 600, 0, 75, 650, 600, 0, 350, 50, 250, 0)
                     + bytes([0]*10) + u32(1, 0, 0, 0) + b'LOKA'
                     + u16(64, 97, 120) + s16(800, -200, 0) + u16(800, 200))
    # One locl feature/lookup for each language. Script default retains glyph 1.
    langs = [('JAN ', 0), ('TRK ', 2), ('ZHS ', 1), ('ZHT ', 1)]
    script_header = 4 + len(langs)*6
    script = u16(script_header, len(langs))
    for i, (tag, _) in enumerate(langs):
        script += tag.encode() + u16(script_header + 6 + i*8)
    script += u16(0, 65535, 0)
    for _, feature in langs:
        script += u16(0, 65535, 1, feature)
    scripts = u16(1) + b'latn' + u16(8) + script
    features = u16(3) + b''.join(b'locl'+u16(20+i*6) for i in range(3))
    features += b''.join(u16(0, 1, i) for i in range(3))
    lookups = u16(3, 8, 28, 48)
    for replacement in [2, 3, 4]:
        lookups += u16(1, 0, 1, 8) + u16(2, 8, 1, replacement) + u16(1, 1, 1)
    # Each lookup is 22 bytes (8 header + 8 substitution + 6 coverage).
    lookups = u16(3, 8, 30, 52) + lookups[8:]
    tables['GSUB'] = u32(0x10000) + u16(10, 10+len(scripts), 10+len(scripts)+len(features))
    tables['GSUB'] += scripts + features + lookups
    count = len(tables); power = 2**(count.bit_length()-1)
    header = u32(0x10000) + u16(count, power*16, power.bit_length()-1, count*16-power*16)
    directory = b''; body = b''; offsets = {}
    for tag, data in sorted(tables.items()):
        offset = 12 + count*16 + len(body); offsets[tag] = offset
        directory += tag.encode() + u32(checksum(data), offset, len(data))
        body += data + b'\0' * (-len(data) % 4)
    result = bytearray(header + directory + body)
    struct.pack_into('>I', result, offsets['head']+8, (0xb1b0afba-checksum(result))&0xffffffff)
    return result


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[3]
    path = Path(sys.argv[1]).resolve()
    if not path.is_relative_to(root / 'build'):
        raise SystemExit('font output must be below this worktree build/')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(font())
