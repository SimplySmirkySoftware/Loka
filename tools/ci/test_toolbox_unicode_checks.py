"""Hand-built PEF mutations and nm output/error contract pins."""
import os
import struct
import tempfile
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests" / "toolbox" / "unicode-abi"))
import check_pef_imports as pef  # noqa: E402
import check_toolbox_startup_wiring as wiring  # noqa: E402
import compare_subset as abi  # noqa: E402
import manifest  # noqa: E402


def fixture(names=("InterfaceLib",), option=0, symbol_class=2):
    strings = bytearray()
    libraries = bytearray()
    symbols = bytearray()
    for i, name in enumerate(names):
        offset = len(strings)
        strings.extend(name.encode() + b"\0")
        libraries.extend(struct.pack(">5IBBH", offset, 0, 0, 1, i, option, 0, 0))
        symbols.extend(struct.pack(">I", (symbol_class << 24) | len(strings)))
        strings.extend(b"symbol\0")
    start = 56 + len(libraries) + len(symbols)
    loader = struct.pack(">14I", 0, 0, 0, 0, 0, 0, len(names), len(names), 0,
                         start, start, start + len(strings), 0, 0) + libraries + symbols + strings
    header = struct.pack(">4s4s4s5IHHI", b"Joy!", b"peff", b"pwpc", 1, 0, 0, 0, 0, 1, 0, 0)
    section = struct.pack(">iIIIII4B", -1, 0, 0, 0, len(loader), 68, 4, 0, 0, 0)
    return header + section + loader


class PefTests(unittest.TestCase):
    def test_exact(self):
        names = ("InterfaceLib", "Textension", "TextEncodingConverter", "UnicodeConverter")
        self.assertEqual(pef.imports(fixture(names)), set(names))

    def test_textension_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "body.pef"
            path.write_bytes(fixture(("InterfaceLib", "Textension")))
            self.assertEqual(pef.check(path, ["InterfaceLib", "Textension"], ["symbol"]),
                             {"InterfaceLib", "Textension"})
            for expected in ([], ["TXNSetData"], ["symbol", "TXNGetData"]):
                with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, "Textension symbols mismatch"):
                    pef.check(path, ["InterfaceLib", "Textension"], expected)

    def test_missing_extra(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.pef"
            for names, expected in [((), ["InterfaceLib"]),
                                    (("InterfaceLib", "Textension"), ["InterfaceLib"])]:
                path.write_bytes(fixture(names))
                with self.assertRaisesRegex(ValueError, "mismatch"):
                    pef.check(path, expected)

    def test_duplicate(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            pef.imports(fixture(("InterfaceLib", "InterfaceLib")))

    def test_weak_library(self):
        with self.assertRaisesRegex(ValueError, "weak library"):
            pef.imports(fixture(option=0x40))

    def test_weak_symbol(self):
        with self.assertRaisesRegex(ValueError, "weak imported symbol"):
            pef.imports(fixture(symbol_class=0x82))

    def test_every_truncation(self):
        data = fixture()
        for end in range(len(data)):
            with self.subTest(end=end), self.assertRaises(ValueError):
                pef.imports(data[:end])

    def test_malformed_offsets_and_ranges(self):
        # Section start, loader strings, library name, count and first symbol.
        for offset in (60, 68 + 40, 68 + 56, 68 + 56 + 12, 68 + 56 + 16):
            data = bytearray(fixture())
            struct.pack_into(">I", data, offset, 0xFFFFFFFF)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                pef.imports(data)

    def test_unterminated_string(self):
        with self.assertRaises(ValueError):
            pef.imports(fixture()[:-1] + b"x")

    def test_missing_file_and_bin(self):
        with self.assertRaises(OSError):
            pef.check(Path("/nonexistent/flavor0.pef"), [])
        with self.assertRaises(ValueError):
            pef.check(Path("app.bin"), [])


class WiringTests(unittest.TestCase):
    def test_defined_code_only(self):
        self.assertTrue(wiring.has_startup("00001234 T " + wiring.SYMBOL))
        for line in (" U " + wiring.SYMBOL, "00001234 D " + wiring.SYMBOL,
                     "00001234 T loka::toolbox::RequireUnicodeSystem()", "",
                     "00001234 T " + wiring.SYMBOL + "other"):
            self.assertFalse(wiring.has_startup(line))

    def test_presence_absence_and_tool_error(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "app.xcoff"
            app.touch()
            defined = subprocess.CompletedProcess([], 0, "1234 T " + wiring.SYMBOL, "")
            empty = subprocess.CompletedProcess([], 0, "", "")
            with patch.object(wiring.subprocess, "run", side_effect=[defined, empty]):
                wiring.check("nm", app, app)
            for results in ([empty], [defined, defined]):
                with patch.object(wiring.subprocess, "run", side_effect=results), self.assertRaises(ValueError):
                    wiring.check("nm", app, app)
            with patch.object(wiring.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "nm")), self.assertRaises(subprocess.CalledProcessError):
                wiring.check("nm", app, app)
            with self.assertRaises(OSError):
                wiring.check("/nonexistent/nm", app, app)
        with self.assertRaises(ValueError):
            wiring.check("nm", app, app)



# Lines in the shape powerpc-apple-macos-objdump prints for an XCOFF object.
OBJDUMP_H = """
Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00000020  00000000  00000000  000000a8  2**2
                  CONTENTS, ALLOC, LOAD, CODE
  1 .data         00000020  00000020  00000020  000000c8  2**3
                  CONTENTS, ALLOC, LOAD, RELOC, DATA
  2 .bss          00000000  00000040  00000040  00000000  2**3
                  ALLOC
"""
OBJDUMP_T = """
SYMBOL TABLE:
[  0](sec -2)(fl 0x00)(ty    0)(scl 103) (nx 1) 0x00000000 probe.cpp
[  6](sec  1)(fl 0x00)(ty    0)(scl   2) (nx 1) 0x00000000 loka_mlte_abi_layout
[ 12](sec  2)(fl 0x00)(ty    0)(scl   2) (nx 1) 0x00000020 loka_mlte_sig_0
[ 16](sec  0)(fl 0x00)(ty    0)(scl   2) (nx 1) 0x00000000 TXNFakeOne
"""
OBJDUMP_S = """
Contents of section .text:
 0000 00000003 01020304 00000004 48000014  ............H...
 0010 60000000 60000000 60000000 60000000  `...`...`...`...
"""


class AbiDriverTests(unittest.TestCase):
    def test_output_wall_accepts_only_the_fixed_root(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            (repo / "build").mkdir(parents=True)
            root = repo / "build" / "toolbox-unicode-abi"
            self.assertEqual(abi.output_wall(root, repo), root.resolve())
            with self.assertRaisesRegex(abi.AbiError, "outside"):
                abi.output_wall(repo / "build" / "elsewhere", repo)
            with self.assertRaisesRegex(abi.AbiError, "outside"):
                abi.output_wall(Path(directory) / "toolbox-unicode-abi", repo)
            outside = Path(directory) / "outside"
            outside.mkdir()
            root.symlink_to(outside, target_is_directory=True)
            with self.assertRaisesRegex(abi.AbiError, "outside"):
                abi.output_wall(root, repo)

    def test_toolchain_paths_are_absolute(self):
        with tempfile.TemporaryDirectory() as directory:
            bin_dir = Path(directory) / "retro" / "toolchain" / "bin"
            bin_dir.mkdir(parents=True)
            for name in ("powerpc-apple-macos-g++", "powerpc-apple-macos-nm", "powerpc-apple-macos-objdump"):
                (bin_dir / name).write_text("")
            previous = Path.cwd()
            try:
                os.chdir(directory)
                tools = abi.Toolchain("retro")
            finally:
                os.chdir(previous)
            self.assertTrue(tools.gxx.is_absolute())
            self.assertEqual(tools.gxx, bin_dir.resolve() / "powerpc-apple-macos-g++")
            with self.assertRaisesRegex(abi.AbiError, "missing tool"):
                abi.Toolchain(Path(directory) / "absent")

    def test_output_wall_refuses_a_symlinked_build_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            repo.mkdir()
            elsewhere = Path(directory) / "elsewhere"
            elsewhere.mkdir()
            (repo / "build").symlink_to(elsewhere, target_is_directory=True)
            with self.assertRaisesRegex(abi.AbiError, "symlink"):
                abi.output_wall(repo / "build" / "toolbox-unicode-abi", repo)

    def test_parse_and_decode(self):
        sections = abi.parse_sections(OBJDUMP_H)
        symbols = abi.parse_symbols(OBJDUMP_T)
        contents = abi.parse_contents(OBJDUMP_S)
        self.assertEqual(sections[0], (".text", 0x20, 0))
        self.assertEqual(symbols["loka_mlte_sig_0"], (2, 0x20))
        values, name = abi.decode_array(sections, symbols, contents, "loka_mlte_abi_layout", 3)
        self.assertEqual((values, name), ([3, 0x01020304, 4], ".text"))
        with self.assertRaisesRegex(abi.AbiError, "leave section"):
            abi.decode_array(sections, symbols, contents, "loka_mlte_abi_layout", 9)
        with self.assertRaisesRegex(abi.AbiError, "symbol missing"):
            abi.decode_array(sections, symbols, contents, "absent", 1)
        with self.assertRaisesRegex(abi.AbiError, "no bytes"):
            abi.decode_array(sections, symbols, {}, "loka_mlte_abi_layout", 1)
        for parse in (abi.parse_sections, abi.parse_symbols, abi.parse_contents):
            with self.subTest(parse=parse.__name__), self.assertRaises(abi.AbiError):
                parse("nothing here\n")

    def test_linkage(self):
        names = {name for name, _, _ in manifest.FUNCTIONS}
        abi.check_linkage(set(names))
        with self.assertRaisesRegex(abi.AbiError, "C\\+\\+ linkage"):
            abi.check_linkage(names | {"_Z6TXNCutP15OpaqueTXNObject"})
        with self.assertRaisesRegex(abi.AbiError, "missing \\['TXNCut'\\]"):
            abi.check_linkage(names - {"TXNCut"})
        with self.assertRaisesRegex(abi.AbiError, "extra \\['TXNActivate'\\]"):
            abi.check_linkage(names | {"TXNActivate"})
        self.assertEqual(abi.parse_undefined("         U TXNFakeOne\n00000020 D loka_mlte_sig_0\n"), {"TXNFakeOne"})

    def test_compare_and_generated_shapes(self):
        labels = abi.layout_labels()
        expected = abi.layout_expected()
        self.assertEqual(len(labels), len(expected))
        self.assertEqual(expected[0], len(expected))
        self.assertEqual(abi.compare(labels, expected, list(expected)), [])
        altered = list(expected)
        altered[-1] ^= 1
        self.assertEqual(len(abi.compare(labels, expected, altered)), 1)
        with self.assertRaisesRegex(abi.AbiError, "lengths"):
            abi.compare(labels, expected, expected[:-1])
        self.assertEqual(abi.layout_source(abi.SUBSET_INCLUDE).count("offsetof("),
                         sum(len(fields) for _, _, _, fields in manifest.TYPES))
        self.assertEqual(abi.signature_source(abi.SDK_INCLUDE).count("__attribute__((used))"),
                         len(manifest.FUNCTIONS))
        self.assertIn(", int)", abi.signature_source(abi.SUBSET_INCLUDE, abi.mismatched_functions()))
        self.assertNotIn('extern "C"', abi.mangled_source())

    def test_manifest_is_consistent(self):
        names = [name for name, _, _, _ in manifest.TYPES]
        self.assertEqual(len(names), len(set(names)))
        for name, size, is_class, fields in manifest.TYPES:
            self.assertEqual(bool(fields), is_class, name)
            for field, offset, field_size in fields:
                self.assertLessEqual(offset + field_size, size, f"{name}.{field}")
        constants = [name for name, _ in manifest.CONSTANTS]
        self.assertEqual(len(constants), len(set(constants)))
        functions = [name for name, _, _ in manifest.FUNCTIONS]
        self.assertEqual(len(functions), len(set(functions)))
        self.assertEqual(manifest.fourcc("TEXT"), 0x54455854)
        with self.assertRaises(ValueError):
            manifest.fourcc("TEX")


if __name__ == "__main__":
    unittest.main()
