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
        self.files = []
        for cpu, example, name, files in disks.PAYLOADS:
            binary = self.root / f"build/retro68/{cpu}/Release/example/{example}/{name}.bin"
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(fake_macbinary(name))
            self.binaries.append(binary)
            if files is None:
                # The shell and CMake readers skip comments and blank lines and
                # trim space, tab and CR; this reader must agree.
                files = ("MAIN.JS", "MINES.JS", "VIEWER.JS", "assets/PB100.PICT")
                manifest = self.root / "example" / example / "disk-scripts.txt"
                manifest.parent.mkdir(parents=True, exist_ok=True)
                manifest.write_bytes(b"# Card scripts\r\n\r\n  MAIN.JS \r\n#NOTES.JS\nMINES.JS\t\n\n\tVIEWER.JS\r\n")
                (manifest.parent / "disk-pictures.txt").write_bytes(b"# Pictures\r\n\n PB100 \t\r\n")
            self.files.append(files)
            for index, filename in enumerate(files):
                data = self.root / "example" / example / filename
                data.parent.mkdir(parents=True, exist_ok=True)
                data.write_bytes(b"original\0bytes\r\n\x80\xff\n" + filename.encode("ascii") * (index + 1))
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
        self.assertEqual(sorted(p.name for p in self.output.glob("*.dsk")),
                         sorted(name + ".dsk" for _, _, name, _ in disks.PAYLOADS))
        self.assertEqual([(cpu, name) for cpu, example, name, _ in disks.PAYLOADS if example == "SmirkyCard"],
                         [("68k", "LokaSmirkyCard68K"), ("ppc", "LokaSmirkyCardPPC")])
        with tempfile.TemporaryDirectory(dir=self.root / "build") as home:
            env = dict(os.environ, HOME=home)
            for binary, files, (_, example, name, _) in zip(self.binaries, self.files, disks.PAYLOADS):
                image = self.output / (name + ".dsk")
                self.assertEqual(image.stat().st_size, 800 * 1024)
                mounted = subprocess.check_output([TOOLS["hmount"], str(image)], env=env, text=True)
                self.assertIn(name, mounted)
                try:
                    listing = subprocess.check_output([TOOLS["hls"], "-1a", ":"], env=env, text=True)
                    self.assertEqual(sorted(listing.splitlines()), sorted({name, *("Pictures" if f.startswith("assets/") else Path(f).name for f in files)}))
                    subprocess.run([TOOLS["hcopy"], "-m", ":" + name, str(Path(home) / "app.bin")], env=env, check=True)
                    self.assertEqual(disks.macbinary(Path(home) / "app.bin"), disks.macbinary(binary))
                    for filename in files:
                        local = Path(home) / (Path(filename).name + ".bin")
                        subprocess.run([TOOLS["hcopy"], "-m", (":Pictures:" if filename.startswith("assets/") else ":") + Path(filename).name, str(local)], env=env, check=True)
                        data = disks.macbinary(local)
                        self.assertEqual(data[2], (self.root / "example" / example / filename).read_bytes())
                        self.assertEqual(data[3], b"")
                        self.assertEqual(data[1], b"TEXTttxt" if filename.endswith(".JS") else b"????UNIX")
                finally:
                    subprocess.run([TOOLS["humount"]], env=env, check=True)

    def test_missing_input_preflight(self):
        smirky = self.root / "example/SmirkyCard"
        for path in [*self.binaries, self.root / "example/ScrapbookUI/ASSETS.LRP",
                     smirky / "disk-scripts.txt", smirky / "MAIN.JS", smirky / "MINES.JS",
                     smirky / "VIEWER.JS"]:
            original = path.read_bytes()
            path.unlink()
            result = self.invoke()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Missing input:", result.stderr)
            self.assertIn(path.name, result.stderr)
            self.assertFalse(self.output.exists())
            path.write_bytes(original)

    def test_manifest_refusals(self):
        manifest = self.root / "example/SmirkyCard/disk-scripts.txt"
        for content, message in ((b"MAIN.JS\n../MINES.JS\n", "not a flat file name"),
                                 (b"MAIN.JS\nCARDS:MINES.JS\n", "not a flat file name"),
                                 (b"MAIN.JS\nMINES.JS\n MAIN.JS\n", "Duplicate card script"),
                                 (b"# nothing\n\n", "No card scripts")):
            manifest.write_bytes(content)
            result = self.invoke()
            self.assertNotEqual(result.returncode, 0, content)
            self.assertIn(message, result.stderr)
            self.assertFalse(self.output.exists())

    def test_size_tiers(self):
        # Boundaries of the three tiers: 800 KiB floppy, 1440 KiB floppy, then
        # a larger image rounded up to a 512 KiB multiple.
        binary = self.root / "sized.bin"
        data = self.root / "sized.data"
        data.write_bytes(b"")
        kib = 1024
        for required, expected in ((800 * kib, 800 * kib), (800 * kib + 1, 1440 * kib),
                                   (1440 * kib, 1440 * kib), (1440 * kib + 1, 1536 * kib),
                                   (1536 * kib, 1536 * kib), (1536 * kib + 1, 2048 * kib)):
            binary.write_bytes(b"x" * (required - 128 * kib))
            self.assertEqual(disks.image_size(binary, [data]), expected, required)
        # Every data file counts toward the requirement.
        binary.write_bytes(b"x" * (800 * kib - 128 * kib - 2))
        extra = self.root / "extra.data"
        extra.write_bytes(b"ab")
        self.assertEqual(disks.image_size(binary, [data, extra]), 800 * kib)
        extra.write_bytes(b"abc")
        self.assertEqual(disks.image_size(binary, [data, extra]), 1440 * kib)

    def test_size_selection_end_to_end(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "ScrapbookUI68K.dsk").stat().st_size, 800 * 1024)
        self.binaries[0].write_bytes(fake_macbinary("ScrapbookUI68K", b"x" * (700 * 1024)))
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.output / "ScrapbookUI68K.dsk").stat().st_size, 1440 * 1024)
        # Fits physically in 1440 KiB but not with the 128 KiB margin, so the
        # disk becomes a 1536 KiB image (PPC SmirkyCard's case) that still
        # mounts and reads back.
        ppc = self.binaries[[name for _, _, name, _ in disks.PAYLOADS].index("LokaSmirkyCardPPC")]
        ppc.write_bytes(fake_macbinary("LokaSmirkyCardPPC", b"x" * (1350 * 1024)))
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        image = self.output / "LokaSmirkyCardPPC.dsk"
        self.assertEqual(image.stat().st_size, 1536 * 1024)
        self.assertEqual((self.output / "LokaSmirkyCard68K.dsk").stat().st_size, 800 * 1024)
        with tempfile.TemporaryDirectory(dir=self.root / "build") as home:
            env = dict(os.environ, HOME=home)
            subprocess.run([TOOLS["hmount"], str(image)], env=env, check=True, stdout=subprocess.DEVNULL)
            try:
                subprocess.run([TOOLS["hcopy"], "-m", ":LokaSmirkyCardPPC", str(Path(home) / "app.bin")],
                               env=env, check=True)
                self.assertEqual(disks.macbinary(Path(home) / "app.bin"), disks.macbinary(ppc))
            finally:
                subprocess.run([TOOLS["humount"]], env=env, check=True)

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

    def test_readback_checks_every_card_script(self):
        # A wrong last script, written under its own name, must be caught too.
        wrong = self.root / "wrong" / "VIEWER.JS"
        wrong.parent.mkdir()
        wrong.write_bytes(b"wrong card script")
        wrapper = self.tools / "hcopy"
        wrapper.unlink()
        wrapper.write_text('#!/usr/bin/env python3\n' +
            'import subprocess, sys\n' +
            f'real = {TOOLS["hcopy"]!r}\n' +
            'args = sys.argv[1:]\n' +
            'if args[0] == "-r" and args[-1] == ":" and args[1].endswith("/SmirkyCard/VIEWER.JS"):\n' +
            f'    args[1] = {str(wrong)!r}\n' +
            'subprocess.run([real, *args], check=True)\n')
        wrapper.chmod(0o755)
        result = self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Data readback differs", result.stderr)
        self.assertIn("VIEWER.JS", result.stderr)
        self.assertFalse((self.output / "LokaSmirkyCard68K.dsk").exists())
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
