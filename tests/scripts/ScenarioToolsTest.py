#!/usr/bin/env python3
"""Characterization tests for the OS-neutral scenario runner tools."""

import json
import shutil
import os
import re
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib


PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SCENARIO_DIR = os.path.join(PROJECT_DIR, "tests", "scenarios")
PNG_TOOL = os.path.join(SCENARIO_DIR, "pngtool.py")

sys.path.insert(0, os.path.join(PROJECT_DIR, "scripts", "rig"))
import golden_identity_guard  # noqa: E402


def resolve_preset_cache(presets, name):
    """CMake unions caches; the first parent wins conflicts, then the child."""
    preset = presets[name]
    parents = preset.get("inherits", [])
    if isinstance(parents, str):
        parents = [parents]
    cache = {}
    for parent in reversed(parents):
        cache.update(resolve_preset_cache(presets, parent))
    cache.update(preset.get("cacheVariables", {}))
    return cache


def run_tool(*arguments):
    return subprocess.run(
        [sys.executable] + list(arguments),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def png_chunk(kind, payload):
    checksum = zlib.crc32(kind)
    checksum = zlib.crc32(payload, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def write_rgb_png(path, width, height, pixels):
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixels[y * width + x])
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    payload = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(rows)))
        + png_chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(payload)


class ExpectedAuditPinsTest(unittest.TestCase):
    def test_startup_identity_declarations_are_tracked_and_win32_guards_before_copy(self):
        declarations = os.path.join(
            SCENARIO_DIR, "startup-golden-identities.txt"
        )
        entries = golden_identity_guard.read_declarations(pathlib.Path(declarations))
        self.assertEqual(
            [cell for cell, _ in entries],
            [
                ("minesweeper", "new-game-twice"),
                ("scrapbook", "flip-forward-back"),
            ],
        )

        runner_path = os.path.join(PROJECT_DIR, "tests", "win32", "run-scenario.ps1")
        with open(runner_path, "r", encoding="utf-8") as handle:
            runner = handle.read()
        guard_call = runner.index("Invoke-GoldenIdentityGuard $Actual")
        golden_copy = runner.index("Copy-Item -LiteralPath $Actual -Destination $Golden")
        self.assertLess(guard_call, golden_copy)
        self.assertIn("--declarations $StartupIdentityDeclarations", runner)

        # The rig's declared capture environment is checked before the golden is
        # written *and* before it is compared, so a bake on a machine that does
        # not match refuses instead of pinning whatever the desktop was.
        resolve_call = runner.index(
            "$RigDescriptor = Resolve-RigDescriptorPath (Get-RigDescriptorDirectory)"
        )
        profile_guard = runner.index(
            "Invoke-CaptureProfileGuard $RigDescriptor $ActualProfile"
        )
        update_branch = runner.index("if ($UpdateGolden) {")
        compare_profile = runner.index("$expectedProfile = Read-ScenarioProfile $GoldenProfile")
        self.assertLess(resolve_call, profile_guard)
        self.assertLess(profile_guard, update_branch)
        self.assertLess(profile_guard, golden_copy)
        self.assertLess(profile_guard, compare_profile)
        self.assertIn("--descriptor $Descriptor", runner)

        # The rig is named by the operator, never inferred. Deriving it from the
        # reported architecture let two machines answer to one descriptor: an
        # isolated guest reports a profile byte-identical to the reference
        # machine's and settles on a different picture (#459). Fixing the runner
        # to one descriptor instead would refuse every other machine, so neither
        # end of that is available -- the name has to come from outside.
        self.assertIn("$env:LOKA_WIN32_RIG", runner)
        self.assertNotIn('"win32-$arch.ini"', runner)

        # The descriptor directory is the operator's, not the tree's. A rig is a
        # fact about one machine, the same as the golden it pins -- and
        # build/win32-scenario/golden is ignored for exactly that reason.
        self.assertNotIn('Join-Path $ProjectDirectory "scripts/rig/win32/rigs"', runner)
        self.assertIn("Resolve-RigDescriptorPath (Get-RigDescriptorDirectory)", runner)

        # The golden is stored under the rig that baked it. Two rigs can share a
        # checkout now that the name is chosen rather than derived, and two rigs
        # reporting the same capture profile can still settle on different pixels
        # (#459) -- so a golden keyed only by example would be overwritten by
        # whichever rig baked last, and the profile comparison would not notice.
        golden_root = runner.index(
            '$GoldenRoot = Join-Path $ProjectDirectory "build/win32-scenario/golden/$RigName"'
        )
        self.assertLess(resolve_call, golden_root)
        self.assertIn("$GoldenDirectory = Join-Path $GoldenRoot $Example", runner)
        self.assertNotIn('"build/win32-scenario/golden/$Example"', runner)
        # The identity guard has to look under the same root, or --update-golden
        # would check one rig's declaration against another rig's stored pixels.
        self.assertNotIn(
            '--golden-root (Join-Path $ProjectDirectory "build/win32-scenario/golden")', runner
        )

        # Named before the vehicle starts: an undeclared machine is turned away
        # without opening a window, and the name is not read off the capture.
        vehicle_start = runner.index("$BuiltExecutable = Join-Path $ProjectDirectory")
        self.assertLess(resolve_call, vehicle_start)

        # Only the example ships. Anything else here would be one machine's rig
        # riding along in everyone's clone.
        rigs = os.path.join(PROJECT_DIR, "scripts", "rig", "win32", "rigs")
        self.assertEqual(sorted(os.listdir(rigs)), ["local.example.ini"])

    def test_win32_vehicle_map_covers_the_shared_registry_examples(self):
        runner_path = os.path.join(PROJECT_DIR, "tests", "win32", "run-scenario.ps1")
        with open(runner_path, "r", encoding="utf-8") as handle:
            runner = handle.read()
        cmake_path = os.path.join(PROJECT_DIR, "win32", "CMakeLists.txt")
        with open(cmake_path, "r", encoding="utf-8") as handle:
            cmake = handle.read()
        with open(os.path.join(PROJECT_DIR, "CMakePresets.json"), "r", encoding="utf-8") as handle:
            presets = json.load(handle)
        win32_tests = next(
            preset
            for preset in presets["buildPresets"]
            if preset["name"] == "win32-tests"
        )

        vehicles = {
            "scrapbook": ("LokaScrapbookScenarioWin32", "ScrapbookUI"),
            "helloworld": ("LokaHelloWorldScenarioWin32", "HelloWorld"),
            "tutorial": ("LokaTutorialScenarioWin32", "Tutorial"),
            "minesweeper": ("LokaMineSweeperScenarioWin32", "MineSweeper"),
            "floppybird": ("LokaFloppyBirdScenarioWin32", "FloppyBird"),
        }
        for example, (target, output_directory) in vehicles.items():
            self.assertIn('"{}" = @{{'.format(example), runner)
            self.assertIn('Executable = "{}.exe"'.format(target), runner)
            self.assertIn('OutputDirectory = "{}"'.format(output_directory), runner)
            self.assertIn(target, cmake)
            self.assertIn(target, win32_tests["targets"])

        self.assertNotIn("currently supports only ScrapbookUI", runner)
        hello_driver_path = os.path.join(
            PROJECT_DIR, "tests", "win32", "HelloWorldScenarioDriver.cpp"
        )
        with open(hello_driver_path, "r", encoding="utf-8") as handle:
            hello_driver = handle.read()
        self.assertIn(
            "SceneScenarioDriver<scenario_tests::HelloWorldScenario>", hello_driver
        )

    def test_scrapbook_refusal_fixtures_have_one_neutral_mapping(self):
        registry = os.path.join(SCENARIO_DIR, "scrapbook-package-fixtures.txt")
        with open(registry, "r", encoding="ascii") as handle:
            entries = [line.split() for line in handle.read().splitlines()]
        self.assertEqual(
            entries,
            [
                ["open-first-page-refused", "corrupt-bag=1"],
                ["refused-flip-keeps-page", "corrupt-bag=3"],
                ["open-text-page-refused", "corrupt-bag=5"],
            ],
        )
        registered = os.path.join(SCENARIO_DIR, "scenarios.txt")
        with open(registered, "r", encoding="ascii") as handle:
            scenarios = set(handle.read().splitlines())
        for scenario, fixture in entries:
            self.assertIn("scrapbook " + scenario, scenarios)
            self.assertRegex(fixture, r"^corrupt-bag=[0-9]+$")

    def test_smirkbench_attributed_cell_has_desktop_vehicles_and_startup_reference(self):
        root = pathlib.Path(PROJECT_DIR)
        presets = json.loads((root / "CMakePresets.json").read_text())
        for rail, suffix, extension in (("macos", "MacOS", "mm"), ("win32", "Win32", "cpp")):
            cmake = root / ("apple/macos/CMakeLists.txt" if rail == "macos" else "win32/CMakeLists.txt")
            target = "LokaSmirkBenchScenario" + suffix
            self.assertIn(target, cmake.read_text())
            preset = next(p for p in presets["buildPresets"]
                          if p["name"] == ("macos-scenarios" if rail == "macos" else "win32-tests"))
            self.assertIn(target, preset["targets"])
            self.assertIn("SmirkBenchScenarioDriver." + extension, cmake.read_text())
            driver = (root / "tests" / rail / ("SmirkBenchScenarioDriver." + extension)).read_text()
            self.assertIn('settings.scenario == "attributed-editor-line"', driver)
            self.assertIn('driver_(settings.scenario == "startup"', driver)
            runner = root / "tests" / rail / ("run-scenario.sh" if rail == "macos" else "run-scenario.ps1")
            self.assertIn("desktop-expected/smirkbench/startup.audit", runner.read_text())
        self.assertTrue((root / "tests/scenarios/desktop-expected/smirkbench/startup.audit").is_file())
        declarations = golden_identity_guard.read_declarations(root / "tests/scenarios/startup-golden-identities.txt")
        self.assertNotIn(("smirkbench", "attributed-editor-line"), [cell for cell, _ in declarations])

    def test_expected_audits_cover_registry_and_pin_app_identity(self):
        registry = os.path.join(PROJECT_DIR, "tests", "scenarios", "scenarios.txt")
        with open(registry, "r", encoding="utf-8") as handle:
            entries = [line.split() for line in handle.read().splitlines()]
        self.assertEqual(len(entries), 33)
        self.assertEqual(len(entries), len({tuple(entry) for entry in entries}))
        self.assertEqual(
            [entry for entry in entries if entry[0] == "simpleviewer"],
            [["simpleviewer", "startup"], ["simpleviewer", "open-sun"], ["simpleviewer", "open-bulb"],
             ["simpleviewer", "churn-replace"], ["simpleviewer", "open-wide-scroll"]],
        )
        self.assertEqual(
            [entry[1] for entry in entries if entry[0] == "smirkycard"],
            ["startup", "laptop-carousel", "laptop-detail"],
        )
        registered_audits = set()
        for entry in entries:
            self.assertEqual(len(entry), 2)
            example, scenario = entry
            audit_path = os.path.join(SCENARIO_DIR, "expected", example, scenario + ".audit")
            # #642 PR 0 is build-only until the delegator measures both partitions.
            # Only this named cell may lack an audit; baked audits use all checks below.
            if entry == ["simpleviewer", "churn-replace"] and not os.path.exists(audit_path):
                self.assertTrue(os.path.isfile(audit_path + ".pending.md"))
                continue
            registered_audits.add(os.path.relpath(audit_path, SCENARIO_DIR))
            with open(audit_path, "rb") as handle:
                audit = handle.read()

            if entry == ["simpleviewer", "churn-replace"]:
                self.assertEqual(audit.count(b"image.load\tok\n"), 10)
            self.assertNotIn(b"\r", audit)
            lines = audit.splitlines(keepends=True)
            self.assertEqual(
                lines[0],
                "loka_scenario_audit version=1 scenario={}\n".format(scenario).encode("ascii"),
            )
            self.assertEqual(lines[-1], b"terminal status=succeeded\n")
            identities = {
                "helloworld": b"HelloWorld",
                "scrapbook": b"ScrapbookUI",
                "tutorial": b"Tutorial",
                "minesweeper": b"MineSweeper",
                "floppybird": b"FloppyBird",
                "smirkbench": b"SmirkBench",
                "simpleviewer": b"SimpleViewer",
                "simpletext": b"SimpleText",
            }
            if example == "smirkycard":
                self.assertIn(b"name=initial-browse\n", audit)
                self.assertIn(b"name=pictures-loaded\n", audit)
                self.assertIn(b"log text=picture.MACPORT%20160x100\n", audit)
                continue
            identity = identities[example]
            self.assertIn(b"test\t" + identity + b"\n", audit)
            self.assertIn(b"step\t" + scenario.encode("ascii") + b"\n", audit)
            self.assertIn(b"status\tok\n", audit)
            self.assertNotIn(b"/home/", audit)
            self.assertNotIn(b"/mnt/", audit)
            self.assertNotIn(b"build/", audit)

        tracked_audits = {
            os.path.relpath(path, SCENARIO_DIR)
            for path in pathlib.Path(SCENARIO_DIR, "expected").glob("*/*.audit")
            if path.name != "standalone-tour.audit"
        }
        self.assertEqual(tracked_audits, registered_audits)

    def test_byte_compare_rejects_observed_string_mutation(self):
        expected = os.path.join(SCENARIO_DIR, "expected", "scrapbook", "open-text-page.audit")
        with tempfile.TemporaryDirectory(prefix="scenario-audit-") as directory:
            actual = os.path.join(directory, "actual.audit")
            with open(expected, "rb") as handle:
                contents = handle.read()
            observed = b"text_matches_package_asset\ttrue\n"
            self.assertIn(observed, contents)
            with open(actual, "wb") as handle:
                handle.write(contents)
            same = subprocess.run(["cmp", expected, actual], check=False)
            self.assertEqual(same.returncode, 0)

            with open(actual, "wb") as handle:
                handle.write(contents.replace(observed, b"text_matches_package_asset\tfalse\n", 1))
            changed = subprocess.run(
                ["cmp", expected, actual],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertNotEqual(changed.returncode, 0)

    def test_startup_audits_reject_per_example_observation_mutations(self):
        mutations = {
            "helloworld": (b"text.value\tLoka Sample\n", b"text.value\tChanged\n"),
            "tutorial": (b"text.value\tLoka Tutorial\n", b"text.value\tChanged\n"),
            "minesweeper": (b"button.text\tNew Game\n", b"button.text\tChanged\n"),
            "floppybird": (b"surface.rects\t72,114,18,14\n", b"surface.rects\t0,0,1,1\n"),
        }
        with tempfile.TemporaryDirectory(prefix="startup-audit-") as directory:
            for example, (observation, mutation) in mutations.items():
                expected = os.path.join(
                    SCENARIO_DIR, "expected", example, "startup.audit"
                )
                actual = os.path.join(directory, example + ".audit")
                with open(expected, "rb") as handle:
                    contents = handle.read()
                self.assertIn(observation, contents)
                with open(actual, "wb") as handle:
                    handle.write(contents.replace(observation, mutation, 1))
                changed = subprocess.run(
                    ["cmp", expected, actual],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                self.assertNotEqual(changed.returncode, 0, example)


class Win32ScenarioVehicleListTest(unittest.TestCase):
    def test_cmake_vehicle_list_matches_runner_table(self):
        """win32/CMakeLists.txt registers a registry row as a Win32 test only for
        examples the PowerShell runner has a vehicle for; the two lists must agree
        or a Toolbox-only cell becomes a guaranteed-failing Win32 test."""
        cmake = open(os.path.join(PROJECT_DIR, "win32", "CMakeLists.txt"), encoding="utf-8").read()
        match = re.search(r"set\(_LOKA_WIN32_SCENARIO_VEHICLES ([a-z0-9 -]+)\)", cmake)
        self.assertIsNotNone(match)
        cmake_vehicles = set(match.group(1).split())
        runner = open(os.path.join(PROJECT_DIR, "tests", "win32", "run-scenario.ps1"), encoding="utf-8").read()
        table = runner[runner.index("$Vehicles = @{"):]
        table = table[:table.index("\n}")]
        runner_vehicles = set(re.findall(r'^\s*"([a-z0-9-]+)" = @\{', table, re.MULTILINE))
        self.assertEqual(cmake_vehicles, runner_vehicles)


class PngToolTest(unittest.TestCase):
    def test_exact_compare_and_diff_pin_one_pixel_change(self):
        with tempfile.TemporaryDirectory(prefix="scenario-png-") as directory:
            expected = os.path.join(directory, "expected.png")
            actual = os.path.join(directory, "actual.png")
            difference = os.path.join(directory, "diff.png")
            write_rgb_png(expected, 2, 2, [(1, 2, 3)] * 4)
            write_rgb_png(actual, 2, 2, [(1, 2, 3), (9, 8, 7), (1, 2, 3), (1, 2, 3)])

            same = run_tool(PNG_TOOL, "compare", expected, expected)
            changed = run_tool(PNG_TOOL, "compare", expected, actual)
            diffed = run_tool(PNG_TOOL, "diff", expected, actual, difference)
            self.assertEqual(same.returncode, 0, same.stderr)
            self.assertNotEqual(changed.returncode, 0)
            self.assertIn("differing pixels: 1", changed.stdout)
            self.assertEqual(diffed.returncode, 1)
            self.assertTrue(os.path.isfile(difference))
            self.assertGreater(os.path.getsize(difference), 0)

    def test_compare_threshold_is_bounded_reported_and_exact_by_default(self):
        with tempfile.TemporaryDirectory(prefix="scenario-png-threshold-") as directory:
            expected = os.path.join(directory, "expected.png")
            actual = os.path.join(directory, "actual.png")
            write_rgb_png(expected, 3, 1, [(1, 2, 3)] * 3)
            write_rgb_png(actual, 3, 1, [(9, 8, 7), (1, 2, 3), (9, 8, 7)])

            unchanged = run_tool(PNG_TOOL, "compare", expected, expected)
            tolerated = run_tool(
                PNG_TOOL, "compare", "--max-diff-px", "2", expected, actual
            )
            over_bound = run_tool(
                PNG_TOOL, "compare", "--max-diff-px", "1", expected, actual
            )
            exact_default = run_tool(PNG_TOOL, "compare", expected, actual)

            self.assertEqual(unchanged.returncode, 0, unchanged.stderr)
            self.assertIn("differing pixels: 0", unchanged.stdout)
            self.assertIn("differing columns: 0", unchanged.stdout)
            self.assertIn("max-diff-px: 0", unchanged.stdout)
            self.assertIn("max-diff-columns: unbounded", unchanged.stdout)
            self.assertEqual(tolerated.returncode, 0, tolerated.stderr)
            self.assertIn("differing pixels: 2", tolerated.stdout)
            self.assertIn("differing columns: 2", tolerated.stdout)
            self.assertIn("max-diff-px: 2", tolerated.stdout)
            self.assertIn("max-diff-columns: unbounded", tolerated.stdout)
            self.assertNotEqual(over_bound.returncode, 0)
            self.assertIn("differing pixels: 2", over_bound.stdout)
            self.assertIn("max-diff-px: 1", over_bound.stdout)
            self.assertIn("first differing pixel (0, 0)", over_bound.stdout)
            self.assertNotEqual(exact_default.returncode, 0)
            self.assertIn("max-diff-px: 0", exact_default.stdout)

    def test_compare_column_threshold_is_bounded_reported_and_unbounded_by_default(self):
        with tempfile.TemporaryDirectory(prefix="scenario-png-columns-") as directory:
            expected = os.path.join(directory, "expected.png")
            actual = os.path.join(directory, "actual.png")
            write_rgb_png(expected, 3, 1, [(1, 2, 3)] * 3)
            write_rgb_png(actual, 3, 1, [(9, 8, 7)] * 3)

            omitted = run_tool(
                PNG_TOOL, "compare", "--max-diff-px", "3", expected, actual
            )
            over_column_bound = run_tool(
                PNG_TOOL,
                "compare",
                "--max-diff-px",
                "400",
                "--max-diff-columns",
                "2",
                expected,
                actual,
            )
            negative = run_tool(
                PNG_TOOL,
                "compare",
                "--max-diff-columns",
                "-1",
                expected,
                actual,
            )
            tolerated = run_tool(
                PNG_TOOL,
                "compare",
                "--max-diff-px",
                "400",
                "--max-diff-columns",
                "3",
                expected,
                actual,
            )

            self.assertEqual(omitted.returncode, 0, omitted.stderr)
            self.assertIn("max-diff-columns: unbounded", omitted.stdout)
            self.assertNotEqual(over_column_bound.returncode, 0)
            self.assertIn(
                "compare result: differing pixels: 3; differing columns: 3; "
                "max-diff-px: 400; max-diff-columns: 2; result: fail",
                over_column_bound.stdout,
            )
            self.assertEqual(tolerated.returncode, 0, tolerated.stderr)
            self.assertNotEqual(negative.returncode, 0)
            self.assertIn(
                "--max-diff-columns must not be negative", negative.stderr
            )

    def test_compare_requires_both_pixel_and_column_bounds(self):
        with tempfile.TemporaryDirectory(prefix="scenario-png-bounds-") as directory:
            expected = os.path.join(directory, "expected.png")
            actual = os.path.join(directory, "actual.png")
            write_rgb_png(expected, 2, 201, [(1, 2, 3)] * 402)
            write_rgb_png(actual, 2, 201, [(9, 8, 7)] * 402)

            over_pixel_bound = run_tool(
                PNG_TOOL,
                "compare",
                "--max-diff-px",
                "400",
                "--max-diff-columns",
                "2",
                expected,
                actual,
            )

            self.assertNotEqual(over_pixel_bound.returncode, 0)
            self.assertIn(
                "compare result: differing pixels: 402; differing columns: 2; "
                "max-diff-px: 400; max-diff-columns: 2; result: fail",
                over_pixel_bound.stdout,
            )

    def test_compare_rejects_dimension_mismatch_at_any_tolerance(self):
        with tempfile.TemporaryDirectory(prefix="scenario-png-dimensions-") as directory:
            expected = os.path.join(directory, "expected.png")
            actual = os.path.join(directory, "actual.png")
            write_rgb_png(expected, 340, 250, [(1, 2, 3)] * (340 * 250))
            write_rgb_png(actual, 340, 251, [(1, 2, 3)] * (340 * 251))

            changed_shape = run_tool(
                PNG_TOOL,
                "compare",
                "--max-diff-px",
                "400",
                "--max-diff-columns",
                "400",
                expected,
                actual,
            )

            self.assertNotEqual(changed_shape.returncode, 0)
            self.assertIn("dimensions differ: 340x250 != 340x251", changed_shape.stdout)
            self.assertIn(
                "compare result: dimension mismatch; result: fail",
                changed_shape.stdout,
            )
            self.assertIn("differing pixels: 340", changed_shape.stdout)
            self.assertIn("differing columns: 340", changed_shape.stdout)
            self.assertIn("max-diff-px: 400", changed_shape.stdout)
            self.assertIn("max-diff-columns: 400", changed_shape.stdout)
            self.assertNotIn("result: pass", changed_shape.stdout)

    def test_win32_compare_records_the_bounded_wobble_and_writes_diff_evidence(self):
        runner_path = os.path.join(PROJECT_DIR, "tests", "win32", "run-scenario.ps1")
        with open(runner_path, "r", encoding="utf-8") as handle:
            runner = handle.read()
        compare_support_path = os.path.join(
            PROJECT_DIR, "tests", "win32", "PngCompare.ps1"
        )
        with open(compare_support_path, "r", encoding="utf-8") as handle:
            compare_support = handle.read()

        compare_call = runner.index(
            "Invoke-PngCompare $Actual $WorkGolden 400 2 *>> $RunnerLog"
        )
        update_branch = runner.index("if ($UpdateGolden) {")
        compare_branch = runner.index("} else {", update_branch)
        diff_on_nonzero = runner.index(
            "if ($PngCompareResult.DifferenceCount -gt 0)", compare_call
        )
        mismatch_failure = runner.index("if ($CompareExitCode -ne 0)", diff_on_nonzero)

        self.assertLess(compare_branch, compare_call)
        self.assertLess(compare_call, diff_on_nonzero)
        self.assertLess(diff_on_nonzero, mismatch_failure)
        self.assertIn("--max-diff-px $MaxDiffPx", compare_support)
        self.assertIn("--max-diff-columns $MaxDiffColumns", compare_support)
        self.assertIn(
            "differing pixels: ([0-9]+); differing columns: ([0-9]+); "
            "max-diff-px: ([0-9]+); max-diff-columns: ([0-9]+)",
            compare_support,
        )
        self.assertIn("$null -eq $PngCompareResult", runner)
        self.assertIn("DifferenceColumnCount = $differenceColumnCount", compare_support)
        self.assertIn("TODO(#459)", runner)
        self.assertIn("169 px each", runner)
        self.assertIn("338 px worst observed", runner)
        self.assertIn("roughly six to eight columns", runner)
        self.assertIn("an icon many more", runner)
        self.assertIn("compositing M1 offscreen composition", runner)


class PresetFlagSeedingTest(unittest.TestCase):
    """Pin the MSVC flag-seeding contract for the win32 preset family.

    Assigning the completed CMAKE_<LANG>_FLAGS / CMAKE_EXE_LINKER_FLAGS cache
    entries from a preset replaces the platform defaults CMake would otherwise
    compose (for MSVC that silently drops /EHsc, which /WX then turns into a
    build break). Presets must seed platform-specific inputs through the
    *_INIT variables instead. The Linux instrumentation presets
    (testing-coverage, testing-asan) intentionally own their complete flag
    sets and are outside this rule.
    """

    COMPLETED_FORMS = ("CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS")

    def load_configure_presets(self):
        with open(os.path.join(PROJECT_DIR, "CMakePresets.json"), "r", encoding="utf-8") as handle:
            return json.load(handle)["configurePresets"]

    def test_win32_presets_never_replace_completed_flag_sets(self):
        checked = 0
        for preset in self.load_configure_presets():
            if not preset["name"].startswith("win32"):
                continue
            checked += 1
            cache = preset.get("cacheVariables", {})
            for completed in self.COMPLETED_FORMS:
                self.assertNotIn(
                    completed,
                    cache,
                    "{} assigns {}; seed it through {}_INIT so MSVC defaults survive".format(
                        preset["name"], completed, completed
                    ),
                )
        self.assertGreater(checked, 0)

    def test_winxp_presets_seed_the_xp_target_through_init(self):
        presets = {p["name"]: p for p in self.load_configure_presets()}
        flags = {
            "CMAKE_C_FLAGS_INIT": "/D_WIN32_WINNT=0x0501 /DWINVER=0x0501",
            "CMAKE_CXX_FLAGS_INIT": "/D_WIN32_WINNT=0x0501 /DWINVER=0x0501",
            "CMAKE_EXE_LINKER_FLAGS_INIT": "/SUBSYSTEM:WINDOWS,5.01",
        }
        self.assertTrue(presets["win32-xp-target"]["hidden"])
        self.assertEqual(presets["win32-xp-target"]["cacheVariables"], flags)
        for name in ("win32-xp-debug", "win32-xp-release",
                     "win32-standalone-x86-xp-release"):
            cache = resolve_preset_cache(presets, name)
            for key, value in flags.items():
                self.assertEqual(cache[key], value, name)
        for name, preset in presets.items():
            if name != "win32-xp-target" and name.startswith("win32"):
                self.assertTrue(flags.keys().isdisjoint(preset.get("cacheVariables", {})), name)

    def test_xp_standalone_presets(self):
        with open(os.path.join(PROJECT_DIR, "CMakePresets.json")) as handle:
            document = json.load(handle)
        presets = {p["name"]: p for p in document["configurePresets"]}
        name = "win32-standalone-x86-xp-release"
        self.assertEqual(presets[name]["inherits"],
                         ["win32-standalone-x86-release", "win32-xp-target"])
        self.assertEqual(presets[name]["binaryDir"],
                         "build/win32/standalone/presentation/x86-xp/Release")
        cache = resolve_preset_cache(presets, name)
        for key, value in {"CMAKE_BUILD_TYPE": "Release",
                           "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreaded",
                           "LOKA_WARNINGS_AS_ERRORS": "ON",
                           "LOKA_ENABLE_STANDALONE_TARGETS": "ON"}.items():
            self.assertEqual(cache[key], value)
        builds = {p["name"]: p for p in document["buildPresets"]}
        for vehicle in ("flow", "loop"):
            build = builds["win32-standalone-{}-x86-xp-release".format(vehicle)]
            self.assertEqual(build["configurePreset"], name)
            self.assertEqual(build["targets"],
                             ["LokaStandalone{}Win32All".format(vehicle.title())])

    def test_win32_profile_decisions(self):
        # PowerShell 7 only: under WSL, powershell.exe resolves to Windows
        # PowerShell, which cannot open this Linux path. Windows CI runs the
        # same test through Win32PresentationStageTest.ps1.
        shell = shutil.which("pwsh")
        if not shell:
            self.skipTest("[skip] pwsh unavailable; Windows runs Win32StandaloneProfileTest.ps1 "
                          "through Win32PresentationStageTest.ps1")
        result = subprocess.run([shell, "-NoProfile", "-File", os.path.join(
            PROJECT_DIR, "tests/scripts/Win32StandaloneProfileTest.ps1")],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)



class StandaloneDebugEntryPointTest(unittest.TestCase):
    """Keep Standalone vehicles out of the ordinary CMake Tools code model."""

    def load_documents(self):
        with open(os.path.join(PROJECT_DIR, "CMakePresets.json"), "r", encoding="utf-8") as handle:
            preset_document = json.load(handle)
            configure_presets = {
                preset["name"]: preset for preset in preset_document["configurePresets"]
            }
            build_presets = {
                preset["name"]: preset for preset in preset_document["buildPresets"]
            }
        with open(os.path.join(PROJECT_DIR, ".vscode", "tasks.json"), "r", encoding="utf-8") as handle:
            task_document = json.load(handle)
        return configure_presets, build_presets, task_document

    def test_standalone_configure_presets_own_separate_build_trees(self):
        configure_presets, _, _ = self.load_documents()
        expected_parents = {
            "macos-standalone-debug": "macos-debug",
            "macos-standalone-release": "macos-release",
            "win32-standalone-debug": "win32-debug",
            "win32-standalone-arm64-release": "win32-arm64-release",
            "win32-standalone-x64-release": "win32-x64-release",
            "win32-standalone-x86-release": "win32-x86-release",
            "retro68-68k-standalone-release": "retro68-68k-release",
            "retro68-68k-standalone-dwarf": "retro68-68k-dwarf",
            "retro68-ppc-standalone-release": "retro68-ppc-release",
        }

        for preset_name, parent_name in expected_parents.items():
            preset = configure_presets[preset_name]
            self.assertEqual(preset["inherits"], parent_name)
            self.assertEqual(
                preset["cacheVariables"]["LOKA_ENABLE_STANDALONE_TARGETS"],
                "ON",
            )
            self.assertNotEqual(
                preset["binaryDir"], configure_presets[parent_name]["binaryDir"]
            )
            self.assertNotIn(
                "LOKA_ENABLE_STANDALONE_TARGETS",
                configure_presets[parent_name].get("cacheVariables", {}),
            )

        with open(os.path.join(PROJECT_DIR, "CMakeLists.txt"), "r", encoding="utf-8") as handle:
            root_cmake = handle.read()
        self.assertRegex(
            root_cmake,
            r"option\(LOKA_ENABLE_STANDALONE_TARGETS\s+[^\)]* OFF\)",
        )

        guarded_targets = {
            "apple/macos/CMakeLists.txt": "LokaStandaloneFlowMacOSAll",
            "win32/CMakeLists.txt": "LokaStandaloneFlowWin32All",
            "tests/toolbox/CMakeLists.txt": "LOKA_STANDALONE_FLOW_TOOLBOX_ALL_TARGET",
        }
        for relative_path, target_name in guarded_targets.items():
            with open(
                os.path.join(PROJECT_DIR, relative_path), "r", encoding="utf-8"
            ) as handle:
                cmake_source = handle.read()
            gate = cmake_source.index("if(LOKA_ENABLE_STANDALONE_TARGETS)")
            target = cmake_source.index(target_name, gate)
            close = cmake_source.index("endif()", gate)
            self.assertLess(gate, target, relative_path)
            self.assertLess(target, close, relative_path)

    def test_release_rails_use_dedicated_standalone_presets_and_build_trees(self):
        expected_fragments = {
            "scripts/macos-standalone-flow.sh": (
                "--preset macos-standalone-release",
                "build/macos/Standalone/Release",
            ),
            "scripts/win32-standalone-flow.ps1": (
                "win32-standalone-$profileArchitecture-release",
                "build/win32/standalone/presentation/$profileArchitecture/Release",
            ),
            "scripts/toolbox-standalone-flow.sh": (
                'CONFIGURE_PRESET="retro68-$CLASSIC_CPU-standalone-release"',
                "build/retro68/$CLASSIC_CPU/Standalone/Release",
            ),
        }
        for relative_path, fragments in expected_fragments.items():
            with open(
                os.path.join(PROJECT_DIR, relative_path), "r", encoding="utf-8"
            ) as handle:
                source = handle.read()
            for fragment in fragments:
                self.assertIn(fragment, source, relative_path)

    def test_win32_release_includes_simpletext(self):
        root_cmake = pathlib.Path(PROJECT_DIR, "CMakeLists.txt").read_text()
        self.assertRegex(
            root_cmake,
            r"add_dependencies\(LokaStandaloneLoopWin32All\s+"
            r"LokaSimpleViewerWin32\s+LokaSimpleTextWin32\)",
        )
        source = pathlib.Path(
            PROJECT_DIR, "scripts/win32-standalone-flow.ps1"
        ).read_text()
        release = source.split("if ($IsReleasePackage) {", 1)[1]
        release = release.split("    exit 0", 1)[0]
        for fragment in (
            '$simpleText = Join-Path $buildRoot "example/SimpleText/LokaSimpleTextWin32.exe"',
            'if (-not (Test-Path -LiteralPath $simpleText -PathType Leaf))',
            'Assert-ExecutableArchitecture $simpleText $Architecture',
            'New-Item -ItemType Directory -Path (Join-Path $Destination "simpletext")',
            'Copy-Item -LiteralPath $simpleText `\n'
            '            -Destination (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe")',
            'Assert-ExecutableArchitecture `\n'
            '            (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe") $Architecture',
            'Assert-FileCopy $simpleText `\n'
            '            (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe")',
            'SimpleViewer and SimpleText remain interactive.',
            'Staged five autonomous loops plus SimpleViewer, SimpleText',
        ):
            self.assertIn(fragment, release)

    def test_win32_release_includes_smirkycard(self):
        root_cmake = pathlib.Path(PROJECT_DIR, "CMakeLists.txt").read_text()
        self.assertRegex(
            root_cmake,
            r"add_subdirectory\(example/SmirkyCard\)\s+"
            r"#[^\n]*\n\s+if\(WIN32 AND TARGET LokaStandaloneLoopWin32All\)\s+"
            r"add_dependencies\(LokaStandaloneLoopWin32All LokaSmirkyCardWin32\)",
        )
        presets = json.loads(
            pathlib.Path(PROJECT_DIR, "CMakePresets.json").read_text())
        by_name = {p["name"]: p for p in presets["configurePresets"]}
        for name in ("win32-standalone-arm64-release",
                     "win32-standalone-x64-release",
                     "win32-standalone-x86-release"):
            self.assertEqual(
                by_name[name]["cacheVariables"].get("LOKA_BUILD_SMIRKYCARD"), "ON", name)
        # The XP release inherits the x86 one, so it stages SmirkyCard too.
        self.assertIn("win32-standalone-x86-release",
                      by_name["win32-standalone-x86-xp-release"]["inherits"])
        source = pathlib.Path(
            PROJECT_DIR, "scripts/win32-standalone-flow.ps1"
        ).read_text()
        release = source.split("if ($IsReleasePackage) {", 1)[1]
        release = release.split("    exit 0", 1)[0]
        for fragment in (
            '$smirkyCard = Join-Path $smirkyCardRoot "LokaSmirkyCardWin32.exe"',
            '$smirkyCardScripts = (Get-SmirkyCardFiles "disk-scripts.txt")',
            '(Get-SmirkyCardFiles "disk-pictures.txt" ".PNG")',
            'throw "SmirkyCard card script not found: $smirkyCardScript"',
            'Assert-ExecutableArchitecture $smirkyCard $Architecture',
            'New-Item -ItemType Directory -Path (Join-Path $Destination "smirkycard")',
            'Assert-FileCopy $smirkyCard `\n'
            '            (Join-Path $Destination "smirkycard/LokaSmirkyCardWin32.exe")',
            'SmirkyCard (experimental) loads MAIN.JS beside it',
            'Staged five autonomous loops plus SimpleViewer, SimpleText and SmirkyCard',
        ):
            self.assertIn(fragment, release)

    def test_debug_presets_cover_flow_and_loop_aggregates(self):
        _, presets, _ = self.load_documents()
        expected = {
            "macos-standalone-flow": (
                "macos-standalone-debug",
                "LokaStandaloneFlowMacOSAll",
            ),
            "macos-standalone-loop": (
                "macos-standalone-debug",
                "LokaStandaloneLoopMacOSAll",
            ),
            "win32-standalone-flow": (
                "win32-standalone-debug",
                "LokaStandaloneFlowWin32All",
            ),
            "win32-standalone-loop": (
                "win32-standalone-debug",
                "LokaStandaloneLoopWin32All",
            ),
            "retro68-68k-standalone-flow-dwarf": (
                "retro68-68k-standalone-dwarf",
                "LokaStandaloneFlow68KAll",
            ),
            "retro68-68k-standalone-loop-dwarf": (
                "retro68-68k-standalone-dwarf",
                "LokaStandaloneLoop68KAll",
            ),
        }

        for preset_name, (configure_preset, target) in expected.items():
            self.assertEqual(presets[preset_name]["configurePreset"], configure_preset)
            self.assertEqual(presets[preset_name]["targets"], [target])

    def test_vscode_keeps_only_release_actions(self):
        _, _, task_document = self.load_documents()
        tasks = {task["label"]: task for task in task_document["tasks"]}
        inputs = {entry["id"]: entry for entry in task_document["inputs"]}

        visible_entry_points = {
            "Standalone: macOS Build / Stage / Verify / Release",
            "Standalone: Win32 Build / Stage / Verify / Release",
            "Standalone: Toolbox 68K Build / Stage / Release",
            "Standalone: Toolbox PPC Build / Stage / Release",
        }
        self.assertEqual(
            {
                label
                for label, task in tasks.items()
                if label.startswith("Standalone:") and not task.get("hide", False)
            },
            visible_entry_points,
        )
        self.assertNotIn("standaloneMode", inputs)
        self.assertEqual(
            tasks["Build & Start in MAME via SCSI"]["dependsOn"],
            ["Build & Prepare SCSI Dev Disk", "MAME: Start"],
        )
        self.assertEqual(
            tasks["Build & Prepare SCSI Dev Disk"]["windows"]["args"][-2:],
            ["${input:lokaScsiApp}", "-BuildAndPrepare"],
        )
        self.assertEqual(
            tasks["Build & Start Loop in MAME via SCSI"]["dependsOn"],
            ["Build & Prepare SCSI Loop Disk", "MAME: Start"],
        )
        self.assertEqual(
            tasks["Build & Prepare SCSI Loop Disk"]["windows"]["args"][-2:],
            ["${input:lokaScsiLoop}", "-BuildAndPrepare"],
        )
        self.assertEqual(
            {
                label
                for label, task in tasks.items()
                if label.startswith("Build: macOS") and not task.get("hide", False)
            },
            {"Build: macOS target", "Build: macOS loop target"},
        )
        self.assertEqual(
            {
                label
                for label, task in tasks.items()
                if label.startswith("Build: Win32") and not task.get("hide", False)
            },
            {"Build: Win32 target", "Build: Win32 loop target"},
        )
        self.assertEqual(
            {
                label
                for label, task in tasks.items()
                if label.startswith("Build: Retro68") and not task.get("hide", False)
            },
            {"Build: Retro68 target", "Build: Retro68 loop target"},
        )

        self.assertEqual(
            tasks["Standalone: macOS Build / Stage / Verify / Release"]["args"],
            ["scripts/macos-standalone-flow.sh", "${input:standaloneReleaseAction}"],
        )
        self.assertEqual(
            tasks["Standalone: Win32 Build / Stage / Verify / Release"]["args"][-2:],
            ["-Action", "${input:standaloneReleaseAction}"],
        )
        self.assertEqual(
            tasks["Standalone: Toolbox 68K Build / Stage / Release"]["args"],
            ["scripts/toolbox-standalone-flow.sh", "${input:toolboxStandaloneReleaseAction}"],
        )
        self.assertEqual(
            tasks["Standalone: Toolbox PPC Build / Stage / Release"]["args"],
            [
                "scripts/toolbox-standalone-flow.sh",
                "${input:toolboxStandaloneReleaseAction}",
                "ppc",
            ],
        )

        self.assertEqual(
            [option["value"] for option in inputs["standaloneReleaseAction"]["options"]],
            ["Build", "Stage", "Verify", "Release"],
        )
        self.assertEqual(
            [option["value"] for option in inputs["toolboxStandaloneReleaseAction"]["options"]],
            ["Build", "Stage", "Release"],
        )

        hidden_dependencies = [
            "Configure: Win32 Native",
            "Configure: Retro68 68K Standalone Release",
            "Build: Retro68 68K Scrapbook Standalone Flow",
            "Build: Retro68 68K HelloWorld Standalone Flow",
            "Build: Retro68 68K Tutorial Standalone Flow",
            "Build: Retro68 68K MineSweeper Standalone Flow",
            "Build: Retro68 68K FloppyBird Standalone Flow",
            "Stage: Toolbox 68K Standalone Flow Release",
        ]
        for label in hidden_dependencies:
            self.assertTrue(tasks[label].get("hide", False), label)

        for label in hidden_dependencies[2:7]:
            self.assertEqual(
                tasks[label]["args"][2], "retro68-68k-standalone-release"
            )
            self.assertEqual(
                tasks[label]["dependsOn"],
                ["Configure: Retro68 68K Standalone Release"],
            )

        removed_variants = [
            "Build: macOS Standalone Flow Release",
            "Stage: macOS Standalone Flow Release",
            "Verify: macOS Standalone Flow Release",
            "Release: macOS Standalone Application Set",
            "Build: Win32 Standalone Flow Release",
            "Stage: Win32 Standalone Flow Release",
            "Verify: Win32 Standalone Flow Release",
            "Release: Win32 Standalone Application Set",
            "Release: Toolbox 68K Standalone Application Set",
        ]
        for label in removed_variants:
            self.assertNotIn(label, tasks)

    def test_vscode_launch_picker_contains_only_ordinary_examples(self):
        with open(
            os.path.join(PROJECT_DIR, ".vscode", "launch.json"),
            "r",
            encoding="utf-8",
        ) as handle:
            launch_document = json.load(handle)

        names = [
            configuration["name"]
            for configuration in launch_document["configurations"]
        ]
        self.assertEqual(
            set(names),
            {
                "Run (macOS HelloWorld)",
                "Run (macOS MineSweeper)",
                "Run (macOS SimpleViewer)",
                "Run (macOS SimpleText)",
                "Run (macOS SmirkBench)",
                "Run (macOS LazyList)",
                "Run (macOS ScrapbookUI)",
                "Run (macOS FloppyBird)",
                "Run (macOS Tutorial)",
                "Run (macOS SmirkyCard)",
                "Run (Windows HelloWorld)",
                "Run (Windows MineSweeper)",
                "Run (Windows SimpleViewer)",
                "Run (Windows SimpleText)",
                "Run (Windows SmirkBench)",
                "Run (Windows LazyList)",
                "Run (Windows ScrapbookUI)",
                "Run (Windows FloppyBird)",
                "Run (Windows Tutorial)",
                "Run (Windows SmirkyCard)",
            },
        )
        self.assertEqual(len(names), len(set(names)))
        self.assertFalse(any("Standalone" in name for name in names))
        self.assertFalse(any("Scenario Loop" in name for name in names))


if __name__ == "__main__":
    unittest.main()
