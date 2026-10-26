#!/usr/bin/env python3
"""Path policy and workflow integration contracts; standard library only."""

import importlib.util
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/ci/ci_paths.py"
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("ci_paths", TOOL)
CI = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CI)
JOBS = ("toolbox-build", "macos", "win32")
WORKFLOWS = ("toolbox", "macos", "windows")
GUARD = "github.event_name != 'pull_request' || steps.changes.outputs.run == 'true'"


class CiPathsTest(unittest.TestCase):
    def check_paths(self, paths, expected):
        for job, run in zip(JOBS, expected):
            with self.subTest(paths=paths, job=job):
                actual, reason = CI.classify(job, paths)
                self.assertEqual(actual, run, reason)
                self.assertTrue(reason)
                if job == "toolbox-build":
                    self.assertEqual(CI.classify("toolbox-ppc", paths), (actual, reason))

    def test_shared_must_run_paths(self):
        for path in ("common/core/State.hpp", "example/hello/main.cpp",
                     "tests/toolbox/scenario.json", "tests/macos/driver.mm",
                     "tests/UnitTest.cpp", "tests/scripts/CiPathsTest.py",
                     "cmake/LokaTestSources.cmake", "CMakeLists.txt"):
            self.check_paths([path], (True, True, True))

    def test_shared_test_sources_run_every_job(self):
        # cmake/LokaTestSources.cmake compiles these into every platform's
        # test executable, so an otherwise skippable apple/toolbox path runs.
        for job in ("macos", "win32", "toolbox-build"):
            run, reason = CI.classify(job, ["apple/toolbox/src/PictParser.cpp"])
            self.assertTrue(run, reason)
            self.assertIn("shared test source", reason)
        run, reason = CI.classify(
            "win32", ["apple/toolbox/src/ToolboxPlatformLayoutHandlers.hpp"])
        self.assertTrue(run, reason)
        self.assertFalse(CI.classify("win32", ["apple/toolbox/src/ToolboxWindow.cpp"])[0])

    def test_platform_trees(self):
        for path, expected in (
            ("apple/toolbox/src/Control.cpp", (True, False, False)),
            ("apple/macos/src/Control.mm", (False, True, False)),
            ("apple/shared.hpp", (True, True, False)),
            ("win32/src/Control.cpp", (False, False, True)),
        ):
            self.check_paths([path], expected)

    def test_docs_only(self):
        self.check_paths(["docs/guide.md", "docs/nested/image.png"],
                         (False, False, False))

    def test_plans_only(self):
        self.check_paths(["plans/proposal.md"], (False, False, False))

    def test_documentation_surfaces_all_jobs(self):
        for paths in (["DESIGN.md", "PHILOSOPHY.md", "docs/RequestDeliveryDesign.md"],
                      ["README.md"], ["AGENTS.md"], ["plans/proposal.md"],
                      [".claude/skills/example/SKILL.md"], ["LICENSE.txt"]):
            for job in CI.JOBS:
                with self.subTest(paths=paths, job=job):
                    self.assertFalse(CI.classify(job, paths)[0])
        for job in CI.JOBS:
            for paths in (["AGENTS.md", "common/x.hpp"], ["common/README.md"],
                          ["unknown/README.md"], ["unknown/LICENSE"], [], ["unknown"]):
                with self.subTest(paths=paths, job=job):
                    self.assertTrue(CI.classify(job, paths)[0])

    def test_tested_documentation_runs_linux_only(self):
        for path in ("docs/smirkycard/loka-style.d.ts", "docs/environments.md",
                     "docs/MAME_DEVELOPMENT.md", "docs/TOOLBOX_STANDALONE_FLOW.md"):
            for job in CI.JOBS:
                with self.subTest(path=path, job=job):
                    self.assertEqual(CI.classify(job, [path])[0], job.startswith("linux-"))
        for job in CI.JOBS:
            self.assertFalse(CI.classify(job, ["docs/RequestDeliveryDesign.md"])[0])

    def test_documentation_inputs_are_derived_from_scripts(self):
        scanner = CI.tested_documentation_inputs
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "tests/scripts/FixtureTest.py"
            script.parent.mkdir(parents=True)
            script.write_text('read("docs/fixture.md")\n'
                              'names = ("split-fixture.md",)\nROOT / "docs" / name\n')
            self.assertEqual(scanner(root), {"docs/fixture.md", "docs/split-fixture.md"})
            with patch.object(CI, "tested_documentation_inputs", lambda: scanner(root)):
                self.assertTrue(CI.classify("linux-headless", ["docs/fixture.md"])[0])
                script.write_text('read("docs/replacement.md")\n')
                self.assertFalse(CI.classify("linux-headless", ["docs/fixture.md"])[0])
                self.assertTrue(CI.classify("linux-headless", ["docs/replacement.md"])[0])

    def test_linux_inputs_and_platform_only_paths(self):
        for job in ("linux-headless", "linux-asan", "linux-release"):
            for path in ("common/x.hpp", "tests/x.cpp", "example/x.cpp",
                         "apple/toolbox/x.cpp", "tools/ci/ci_paths.py", "cmake/x.cmake",
                         "CMakeLists.txt", "CMakePresets.json", "scripts/x.sh",
                         ".github/workflows/linux.yml"):
                with self.subTest(job=job, path=path):
                    self.assertTrue(CI.classify(job, [path])[0])
            for path in ("win32/x.cpp", "apple/macos/x.mm"):
                self.assertFalse(CI.classify(job, [path])[0])
        self.check_paths(["apple/toolbox/x.cpp"], (True, False, False))

    def test_workflow_self_change_overrides_skip(self):
        for index, workflow in enumerate(WORKFLOWS):
            self.check_paths(["docs/guide.md",
                              ".github/workflows/" + workflow + ".yml"],
                             tuple(i == index for i in range(3)))
        self.check_paths([".github/workflows/linux.yml"], (False, False, False))
        self.check_paths([".github/workflows/size-bank-refresh.yml"],
                         (False, False, False))

    def test_bank_json_only(self):
        self.check_paths(["tools/ci/retro68_68k_size_baseline.json"],
                         (True, False, False))

    def test_retro68_tools(self):
        for path in ("tools/ci/retro68_size_report.py",
                     "tools/ci/retro68_size_bank_refresh.py"):
            self.check_paths([path], (True, False, False))

    def test_mixed_diff_cannot_hide_run_path(self):
        for paths, expected in (
            (["docs/guide.md", "common/core/State.hpp"], (True, True, True)),
            (["common/core/State.hpp", "docs/guide.md"], (True, True, True)),
            (["apple/toolbox/a.cpp", "win32/a.cpp"], (True, False, True)),
        ):
            self.check_paths(paths, expected)

    def test_empty_diff_runs(self):
        self.check_paths([], (True, True, True))

    def test_unknown_paths_run(self):
        for path in ("CMakePresets.json", "assets/icon.png",
                     "tools/ci/ci_paths.py", "tools/lrpc/main.cpp",
                     "docs-extra/file.md", "", '"docs/quoted\\nname.md"'):
            self.check_paths([path], (True, True, True))

    def test_reason_records_rule_or_conservative_fallback(self):
        self.assertIn("must-run common/*", CI.classify("macos", ["common/a"])[1])
        self.assertIn("outside may-skip", CI.classify("macos", ["unknown"])[1])
        self.assertIn("own workflow", CI.classify(
            "macos", [".github/workflows/macos.yml"])[1])
        self.assertIn("may-skip: docs/*, win32/*", CI.classify(
            "macos", ["docs/a", "win32/b"])[1])
        self.assertNotIn("\n", CI.classify("macos", ["unknown\nrun=false"])[1])

    def test_cli_argv_stdin_multiple_jobs_and_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            summary = Path(directory) / "summary"
            command = [sys.executable, str(TOOL), "--summary", str(summary)]
            for job in JOBS:
                command += ["--job", job]
            argv = subprocess.check_output(command + ["docs/guide.md"], text=True)
            stdin = subprocess.check_output(command, input="docs/guide.md\n", text=True)
            self.assertEqual(argv, stdin)
            self.assertEqual(argv.count("run=false\n"), 3)
            self.assertEqual(len(summary.read_text().splitlines()), 6)
            empty = subprocess.check_output(command, input="", text=True)
            self.assertEqual(empty.count("run=true\n"), 3)
            self.assertEqual(len(summary.read_text().splitlines()), 6)
            bad = subprocess.run([sys.executable, str(TOOL), "--job", "typo"],
                                 input="docs/a\n", capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertNotIn("run=false", bad.stdout)

    def test_workflow_guards_preserve_existing_conditions(self):
        for name, job in (*zip(WORKFLOWS, JOBS), ("toolbox", "toolbox-ppc"),
                          *(("linux", job) for job in CI.JOBS if job.startswith("linux-"))):
            workflow = (ROOT / ".github/workflows" / (name + ".yml")).read_text()
            job_body = workflow.split("  " + job + ":\n", 1)[1]
            job_body = re.split(r"^  [a-zA-Z0-9_-]+:\n", job_body, maxsplit=1,
                                flags=re.MULTILINE)[0]
            steps = re.split(r"^      - ", job_body.split("    steps:\n", 1)[1],
                             flags=re.MULTILINE)[1:]
            self.assertIn("fetch-depth: 0", steps[0])
            self.assertNotIn("        if:", steps[0])
            self.assertEqual(steps[1].count("        if:"), 1)
            self.assertIn("id: changes", steps[1])
            self.assertIn("if: ${{ github.event_name == 'pull_request' }}", steps[1])
            self.assertIn("shell: bash", steps[1])
            self.assertIn("--job " + job, steps[1])
            for step in steps[2:]:
                with self.subTest(workflow=name, step=step.splitlines()[0]):
                    self.assertEqual(step.count("        if:"), 1)
                    # The Toolbox comment step deliberately runs on every same-repo
                    # PR (a reverted, now-skippable diff must clear its warning).
                    if name == "toolbox" and "size headroom comment" in step:
                        continue
                    self.assertIn(GUARD, step)
            if name == "macos":
                self.assertIn("failure() && (" + GUARD + ")", steps[-1])
            if job == "toolbox-ppc":
                self.assertEqual(len(steps), 5)
                self.assertIn("Check Toolbox Unicode flavor contracts", steps[-1])
                self.assertNotIn("headroom", job_body)
                self.assertIn("    permissions:\n      contents: read\n", job_body)
                self.assertNotIn("pull-requests:", job_body)
            if job == "toolbox-build":
                self.assertIn("!cancelled() && github.event_name == 'pull_request'", steps[-2])
                self.assertIn("--check-headroom 1024", steps[-2])
                self.assertNotIn(GUARD, steps[-1])
                self.assertIn("!cancelled() && github.event_name == 'pull_request'", steps[-1])
                self.assertIn("github.event.pull_request.head.repo.full_name == github.repository", steps[-1])
                self.assertIn("fs.existsSync(reportPath)", steps[-1])

    def test_workflow_diff_handles_deletion_rename_and_failure(self):
        # Execute the actual Bash step against a local git history, including
        # a rename from a must-run tree into docs and a missing base ref.
        workflow = (ROOT / ".github/workflows/macos.yml").read_text()
        step = workflow.split("      - name: Classify PR paths\n", 1)[1]
        script = re.search(r"        run: \|\n((?:          .*\n)+)", step)[1]
        script = "\n".join(line[10:] for line in script.splitlines())
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            def git(*args):
                return subprocess.check_output(["git", *args], cwd=repo,
                                               stderr=subprocess.STDOUT, text=True)
            git("init", "-q")
            git("config", "user.name", "CI Test")
            git("config", "user.email", "ci-test@example.invalid")
            (repo / "common").mkdir()
            (repo / "common/input").write_text("fixture\n")
            git("add", ".")
            git("commit", "-qm", "base")
            git("update-ref", "refs/remotes/origin/main", "HEAD")
            (repo / "tools/ci").mkdir(parents=True)
            (repo / "tools/ci/ci_paths.py").write_text(TOOL.read_text())
            (repo / "docs").mkdir()
            (repo / "docs/guide").write_text("guide\n")
            git("add", "docs")
            git("commit", "-qm", "docs")
            env = dict(os.environ, BASE="main", RUNNER_TEMP=directory,
                       GITHUB_OUTPUT=str(repo / "output"),
                       GITHUB_STEP_SUMMARY=str(repo / "summary"))
            def run():
                (repo / "output").write_text("")
                return subprocess.run(["bash", "--noprofile", "--norc", "-eo",
                                       "pipefail", "-c", script], cwd=repo,
                                      env=env, capture_output=True, text=True)
            self.assertEqual(run().returncode, 0)
            self.assertIn("run=false\n", (repo / "output").read_text())
            self.assertEqual(len((repo / "summary").read_text().splitlines()), 1)
            git("mv", "common/input", "docs/input")
            git("commit", "-qm", "move to docs")
            self.assertEqual(run().returncode, 0)
            self.assertIn("run=true\n", (repo / "output").read_text())
            self.assertIn("common/input", (repo / "ci-paths.txt").read_text())
            env["BASE"] = "missing"
            self.assertNotEqual(run().returncode, 0)
            self.assertEqual((repo / "output").read_text(), "")


if __name__ == "__main__":
    unittest.main()
