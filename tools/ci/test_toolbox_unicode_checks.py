"""Hand-built PEF mutations and nm output/error contract pins."""
import struct
import tempfile
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

import check_pef_imports as pef
import check_toolbox_startup_wiring as wiring


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


if __name__ == "__main__":
    unittest.main()
