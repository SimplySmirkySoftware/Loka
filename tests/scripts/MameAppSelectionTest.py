"""Check CPU routing without invoking a compiler or modifying a disk."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SelectionTest(unittest.TestCase):
    def test_cpu_routes_build_stage_and_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scripts = root / "scripts"
            scripts.mkdir()
            cards = root / "example/SmirkyCard"
            cards.mkdir(parents=True)
            manifest = cards / "disk-scripts.txt"
            shutil.copy2(ROOT / "example/SmirkyCard/disk-scripts.txt", manifest)
            shutil.copy2(ROOT / "example/SmirkyCard/disk-pictures.txt", cards)
            shutil.copy2(ROOT / "scripts/mame-dev-disk-app.sh", scripts)
            for name in ("retro68-cmake.sh", "mame-dev-disk.sh", "toolbox-standalone-flow.sh"):
                path = scripts / name
                path.write_text(
                    '#!/bin/bash\n'
                    'printf "%s|%s|%s\\n" "${0##*/}" "$LOKA_MAME_CPU" "$*" >> "$CALL_LOG"\n'
                    '[ "${FAIL_BUILD:-0}" != 1 ] || exit 7\n'
                )
                path.chmod(0o755)
            for cpu, suffix in (("68k", "68K"), ("ppc", "PPC")):
                env = dict(os.environ, LOKA_RETRO68_SELECTED_PRESET="retro68-" + cpu + "-release",
                           CALL_LOG=str(root / "calls"))
                for key, target, location in (
                    ("HelloWorld", "LokaHello" + suffix, "Release/example/HelloWorld"),
                    ("SimpleText", "LokaSimpleText" + suffix, "Release/example/SimpleText"),
                    ("SmirkyCard", "LokaSmirkyCard" + suffix, "Release/example/SmirkyCard"),
                    ("ScrapbookUI", "ScrapbookUI" + suffix, "Release/example/ScrapbookUI"),
                    ("FloppyBirdStandaloneLoop", "LokaFloppyStandaloneLoop" + suffix,
                     "Standalone/Release/tests/toolbox"),
                ):
                    Path(env["CALL_LOG"]).write_text("")
                    subprocess.run(["bash", str(scripts / "mame-dev-disk-app.sh"),
                                    "--build-and-prepare", key], env=env, check=True)
                    calls = Path(env["CALL_LOG"]).read_text()
                    self.assertIn("--target " + target + "_APPL", calls)
                    self.assertIn("build/retro68/" + cpu + "/" + location + "/" + target + ".bin", calls)
                    self.assertIn("mame-dev-disk.sh|" + cpu + "|", calls)
                    if key == "SmirkyCard":
                        for line in (cards / "disk-pictures.txt").read_text().splitlines():
                            stem = line.strip()
                            if not stem or stem.startswith("#"):
                                continue
                            self.assertIn(str(cards / "assets" / (stem + ".PICT")), calls)
                        with self.subTest(cpu=cpu, manifest="original"):
                            self.assertIn(str(root / "example/SmirkyCard/MAIN.JS") + " " +
                                          str(root / "example/SmirkyCard/MINES.JS") + " " +
                                          str(root / "example/SmirkyCard/VIEWER.JS") + " ", calls)
                    elif key == "ScrapbookUI":
                        self.assertIn(str(root / ("build/retro68/" + cpu +
                                                 "/Release/example/ScrapbookUI/ASSETS.LRP")) + "\n", calls)
                original_manifest = manifest.read_bytes()
                manifest.write_bytes(original_manifest + b"\r\n  # extra card\r\n\r\n  FOURTH.JS  ")
                Path(env["CALL_LOG"]).write_text("")
                subprocess.run(["bash", str(scripts / "mame-dev-disk-app.sh"),
                                "SmirkyCard"], env=env, check=True)
                calls = Path(env["CALL_LOG"]).read_text()
                with self.subTest(cpu=cpu, manifest="fourth card"):
                    self.assertIn(" ".join(str(cards / name) for name in
                                           ("MAIN.JS", "MINES.JS", "VIEWER.JS", "FOURTH.JS")) + " ", calls)
                manifest.write_bytes(original_manifest)
                for key, flag in (("All", "--all"), ("AllStandaloneLoops", "--all-loops"),
                                  ("AllStandaloneFlows", "--all-flows")):
                    Path(env["CALL_LOG"]).write_text("")
                    subprocess.run(["bash", str(scripts / "mame-dev-disk-app.sh"),
                                    "--build-and-prepare", key], env=env, check=True)
                    self.assertIn("mame-dev-disk.sh|" + cpu + "|" + flag,
                                  Path(env["CALL_LOG"]).read_text())
                Path(env["CALL_LOG"]).write_text("")
                subprocess.run(["bash", str(scripts / "mame-dev-disk-app.sh"),
                                "--build-and-prepare", "ScrapbookStandaloneFlow"], env=env, check=True)
                calls = Path(env["CALL_LOG"]).read_text()
                self.assertIn("Stage " + cpu, calls)
                self.assertIn("toolbox-" + cpu + "-release", calls)
                env["FAIL_BUILD"] = "1"
                Path(env["CALL_LOG"]).write_text("")
                failed = subprocess.run(["bash", str(scripts / "mame-dev-disk-app.sh"),
                                         "--build-and-prepare", "AllStandaloneLoops"], env=env)
                self.assertNotEqual(failed.returncode, 0)
                self.assertNotIn("mame-dev-disk.sh|", Path(env["CALL_LOG"]).read_text())

    def test_invalid_selection_and_task_wiring(self):
        result = subprocess.run(["bash", str(ROOT / "scripts/mame-dev-disk-app.sh"),
                                 "--target", "HelloWorld"],
                                env=dict(os.environ, LOKA_RETRO68_SELECTED_PRESET="win32-debug"),
                                capture_output=True)
        self.assertEqual(result.returncode, 2)
        tasks = json.loads((ROOT / ".vscode/tasks.json").read_text())["tasks"]
        for task in tasks:
            if task.get("command") == "${workspaceFolder}/scripts/mame-dev-disk-app.sh":
                self.assertEqual(task["options"]["env"]["LOKA_RETRO68_SELECTED_PRESET"],
                                 "${command:cmake.activeConfigurePresetName}")


if __name__ == "__main__":
    unittest.main()
