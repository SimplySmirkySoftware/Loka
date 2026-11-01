#!/usr/bin/env python3
"""Require an exact set of strong PPC PEF imports (never MacBinary input).

Layout authority: Retro68/PEFTools/PEFBinaryFormat.h. All offsets are checked
before use; library symbol ranges must partition the imported symbol table.
"""
import argparse
from pathlib import Path
import struct
import sys


def imports(data, symbol_sets=None):
    def bounds(offset, size, limit=len(data)):
        if offset < 0 or size < 0 or offset > limit or size > limit - offset:
            raise ValueError("truncated or malformed bounds")

    bounds(0, 40)
    if data[:12] != b"Joy!peffpwpc" or struct.unpack_from(">I", data, 12)[0] != 1:
        raise ValueError("not a version-1 PPC PEF")
    sections, instantiated = struct.unpack_from(">HH", data, 32)
    if instantiated > sections:
        raise ValueError("invalid instantiated section count")
    table_end = 40 + 28 * sections
    bounds(40, 28 * sections)
    loaders = []
    occupied = []
    for i in range(sections):
        _, _, total, unpacked, size, offset, kind, _, _, _ = struct.unpack_from(
            ">iIIIII4B", data, 40 + 28 * i)
        bounds(offset, size)
        if unpacked > total or (size and offset < table_end):
            raise ValueError("invalid section bounds")
        if size:
            occupied.append((offset, offset + size))
        if kind == 4:
            # Loader sections are non-instantiated: MakePEF writes zero
            # total/unpacked lengths. Only containerLength bounds their bytes.
            loaders.append(data[offset:offset + size])
    occupied.sort()
    if any(a[1] > b[0] for a, b in zip(occupied, occupied[1:])):
        raise ValueError("overlapping sections")
    if len(loaders) != 1:
        raise ValueError("expected exactly one loader section")
    loader = loaders[0]
    bounds(0, 56, len(loader))
    fields = struct.unpack_from(">14I", loader)
    libraries, symbols, relocations, reloc_offset, strings, exports = fields[6:12]
    symbol_start = 56 + libraries * 24
    reloc_start = symbol_start + symbols * 4
    metadata_end = reloc_start + relocations * 12
    bounds(56, metadata_end - 56, len(loader))
    if not metadata_end <= reloc_offset <= strings <= exports <= len(loader):
        raise ValueError("invalid loader table offsets")

    def name(offset):
        start = strings + offset
        if start < strings or start >= exports:
            raise ValueError("invalid string offset")
        end = loader.find(b"\0", start, exports)
        if end < 0 or end == start:
            raise ValueError("unterminated or empty import name")
        try:
            return loader[start:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValueError("non-ASCII import name") from error

    names = set()
    ranges = []
    owners = []
    for i in range(libraries):
        offset, _, _, count, first, options, _, _ = struct.unpack_from(
            ">5IBBH", loader, 56 + 24 * i)
        library = name(offset)
        if library in names:
            raise ValueError("duplicate library: " + library)
        names.add(library)
        if options & 0x40:
            raise ValueError("weak library: " + library)
        bounds(first, count, symbols)
        ranges.append((first, first + count))
        owners.append((library, first, first + count))
    end = 0
    for first, last in sorted(ranges):
        if first != end:
            raise ValueError("overlapping or unowned import symbol range")
        end = last
    if end != symbols:
        raise ValueError("unowned import symbols")
    for i in range(symbols):
        symbol = struct.unpack_from(">I", loader, symbol_start + 4 * i)[0]
        if symbol & 0x80000000:
            raise ValueError("weak imported symbol")
        symbol_name = name(symbol & 0x00FFFFFF)
        if symbol_sets is not None:
            for library, first, last in owners:
                if first <= i < last:
                    symbol_sets.setdefault(library, set()).add(symbol_name)
                    break
    return names


def check(path, expected, textension_symbols=None):
    if path.suffix != ".pef":
        raise ValueError("input must be .pef, not .bin")
    symbols = {}
    actual = imports(path.read_bytes(), symbols)
    if actual != set(expected):
        raise ValueError(f"imports mismatch: missing={sorted(set(expected) - actual)}, "
                         f"extra={sorted(actual - set(expected))}")
    if textension_symbols is not None:
        actual_symbols = symbols.get("Textension", set())
        expected_symbols = set(textension_symbols)
        if actual_symbols != expected_symbols:
            raise ValueError(f"Textension symbols mismatch: missing={sorted(expected_symbols - actual_symbols)}, "
                             f"extra={sorted(actual_symbols - expected_symbols)}")
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pef", type=Path)
    parser.add_argument("libraries", nargs="+")
    parser.add_argument("--textension-symbols", nargs="+", help="exact strong Textension symbol set")
    args = parser.parse_args()
    try:
        print(f"{args.pef}: strong imports {', '.join(sorted(check(args.pef, args.libraries, args.textension_symbols)))}")
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
