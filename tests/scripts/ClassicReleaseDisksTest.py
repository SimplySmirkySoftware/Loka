#!/usr/bin/env python3
"""Real hfsutils round trips, refusals and readback fault injection."""

import binascii
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("classic_disks", ROOT / "scripts/release/classic-disks.py")
disks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(disks)


def fake_macbinary(name, data=b"data\x00\r\nfork", resource=b"resource\x00fork"):
    header = bytearray(128)
    header[1] = len(name)
    header[2:2 + len(name)] = name.encode("ascii")
    header[65:73] = b"APPLTEST"
    struct.pack_into(">II", header, 83, len(data), len(resource))
    header[122:124] = b"\x81\x81"
    struct.pack_into(">H", header, 124, binascii.crc_hqx(header[:124], 0))
    return bytes(header) + data + b"\0" * (-len(data) % 128) + resource + b"\0" * (-len(resource) % 128)


class ClassicReleaseDisksTest(unittest.TestCase):
    def setUp(self):
        # Generated fixture disks stay under ignored build/, including on failure.
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="classic-disk-test-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("release/classic-disks.py", "hfs-disk.sh", "retro68-env.sh", "env-file.sh"):
            target = self.root / "scripts" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / "scripts" / name, target)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        self.binaries = []
        for cpu, example, name, filename in disks.PAYLOADS:
            binary = self.root / f"build/retro68/{cpu}/Release/example/{example}/{name}.bin"
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(fake_macbinary(name))
            self.binaries.append(binary)
            data = self.root / "example" / example / filename
            data.parent.mkdir(parents=True, exist_ok=True)
            data.write_bytes(b"original\0bytes\r\n\x80\xff\n")
        self.output = self.root / "build/release/classic-disks"
        self.tools = self.root / "tools"
        self.tools.mkdir()
        for name, path in TOOLS.items():
            (self.tools / name).symlink_to(path)
        self.env = dict(os.environ, RETRO68_TOOLCHAIN_BIN=str(self.tools),
                        RETRO68_ENV_FILE=str(self.root / "absent.env"))

    def invoke(self):
        return subprocess.run([sys.executable, str(self.root / "scripts/release/classic-disks.py")],
                              env=self.env, capture_output=True, text=True)

    def test_roundtrip(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(list(self.output.glob("*.dsk"))), 3)
        with tempfile.TemporaryDirectory(dir=self.root / "build") as home:
            env = dict(os.environ, HOME=home)
            for binary, (_, example, name, filename) in zip(self.binaries, disks.PAYLOADS):
                image = self.output / (name + ".dsk")
                self.assertEqual(image.stat().st_size, 800 * 1024)
                mounted = subprocess.check_output([TOOLS["hmount"], str(image)], env=env, text=True)
                self.assertIn(name, mounted)
                try:
                    listing = subprocess.check_output([TOOLS["hls"], "-1a", ":"], env=env, text=True)
                    self.assertEqual(sorted(listing.splitlines()), sorted((name, filename)))
                    for hfs_name, local in ((name, "app.bin"), (filename, "data.bin")):
                        subprocess.run([TOOLS["hcopy"], "-m", ":" + hfs_name, str(Path(home) / local)], env=env, check=True)
                    self.assertEqual(disks.macbinary(Path(home) / "app.bin"), disks.macbinary(binary))
                    data = disks.macbinary(Path(home) / "data.bin")
                    self.assertEqual(data[2], (self.root / "example" / example / filename).read_bytes())
                    self.assertEqual(data[3], b"")
                    self.assertEqual(data[1], b"TEXTttxt" if filename == "MAIN.JS" else b"????UNIX")
                finally:
                    subprocess.run([TOOLS["humount"]], env=env, check=True)

    def test_missing_input_preflight(self):
        for path in [*self.binaries, self.root / "example/ScrapbookUI/ASSETS.LRP",
                     self.root / "example/SmirkyCard/MAIN.JS"]:
            original = path.read_bytes()
            path.unlink()
            result = self.invoke()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Missing input:", result.stderr)
            self.assertFalse(self.output.exists())
            path.write_bytes(original)

    def test_size_selection_and_refusal(self):
        self.binaries[0].write_bytes(fake_macbinary("ScrapbookUI68K", b"x" * (700 * 1024)))
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "ScrapbookUI68K.dsk").stat().st_size, 1440 * 1024)
        previous = (self.output / "ScrapbookUI68K.dsk").read_bytes()
        # Fits physically in 1440 KiB but violates the explicit 128 KiB margin.
        self.binaries[0].write_bytes(fake_macbinary("ScrapbookUI68K", b"x" * (1350 * 1024)))
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not fit", result.stderr)
        self.assertEqual((self.output / "ScrapbookUI68K.dsk").read_bytes(), previous)

    def test_readback_refuses_corruption(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        previous = (self.output / "ScrapbookUI68K.dsk").read_bytes()
        # Inject successful writes with incorrect contents: exit-code checks alone
        # cannot detect these. Cover data, each application fork and membership.
        for fault in ("data", "data-fork", "resource-fork", "extra"):
            bad = self.root / "bad.bin"
            bad.write_bytes(fake_macbinary("ScrapbookUI68K",
                data=b"wrong" if fault == "data-fork" else b"data\x00\r\nfork",
                resource=b"wrong" if fault == "resource-fork" else b"resource\x00fork"))
            (self.root / "extra").write_bytes(b"wrong")
            wrapper = self.tools / "hcopy"
            wrapper.unlink()
            wrapper.write_text('#!/usr/bin/env python3\n' +
                'import subprocess, sys\n' +
                f'real = {TOOLS["hcopy"]!r}\n' +
                'args = sys.argv[1:]\n' +
                f'fault = {fault!r}\n' +
                'if args[0] == "-m" and args[-1] == ":" and "ScrapbookUI68K.bin" in args[1] and fault.endswith("-fork"):\n' +
                f'    args[1] = {str(bad)!r}\n' +
                'subprocess.run([real, *args], check=True)\n' +
                'if args[0] == "-r" and args[-1] == ":":\n' +
                '    if fault in ("data", "extra"):\n' +
                f'        subprocess.run([real, "-r", {str(self.root / "extra")!r}, ":ASSETS.LRP" if fault == "data" else ":extra"], check=True)\n')
            wrapper.chmod(0o755)
            result = self.invoke()
            self.assertNotEqual(result.returncode, 0, fault)
            self.assertRegex(result.stderr, "differs|differ|Unexpected root")
            self.assertEqual((self.output / "ScrapbookUI68K.dsk").read_bytes(), previous)
            self.assertFalse(list(self.output.glob(".stage-*")))

    def test_output_walls(self):
        self.output.parent.mkdir(parents=True)
        self.output.symlink_to(self.root / "example", target_is_directory=True)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr)
        self.output.unlink()
        self.output.mkdir()
        tracked = self.output / "ScrapbookUI68K.dsk"
        tracked.write_bytes(b"tracked sentinel")
        subprocess.run(["git", "-C", str(self.root), "add", "-f", str(tracked)], check=True)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked output", result.stderr)
        self.assertEqual(tracked.read_bytes(), b"tracked sentinel")


if __name__ == "__main__":
    try:
        TOOLS = disks.retro68_tools()
    except (OSError, subprocess.CalledProcessError):
        print("[skip] Classic release disk round trips: Retro68 hfsutils not found")
        sys.exit(0)
    unittest.main()
