#!/usr/bin/env python3
"""Conservative job eligibility for #700; unknown paths and empty diffs run.

Globs match repository-relative paths (fnmatch's * includes slashes). Specific
may-skip exceptions precede broad run globs, except a job's own workflow always
runs. Git's quoted unusual filenames deliberately fall back to running.
"""

import argparse
from fnmatch import fnmatchcase
import json
import re
from pathlib import Path
import sys


# Shared documentation surfaces; slash-free globs apply only at the root.
DOCS_ONLY = ("*.md", "plans/*", ".claude/*", "LICENSE*")


JOBS = {
    "toolbox-build": {
        "workflow": ".github/workflows/toolbox.yml",
        "run": ("common/*", "apple/toolbox/*", "example/*", "tests/*",
                "tools/ci/retro68_*", "cmake/*", "CMakeLists.txt"),
        "may_skip": DOCS_ONLY + ("docs/*", "win32/*", "apple/macos/*",
                     ".github/workflows/*.yml"),
    },
    "macos": {
        "workflow": ".github/workflows/macos.yml",
        "run": ("common/*", "apple/*", "example/*", "tests/*",
                "cmake/*", "CMakeLists.txt"),
        # Toolbox-only exceptions to apple/*; other workflows cannot change
        # this job. Shared tests and build inputs still run it (including #698).
        "may_skip": DOCS_ONLY + ("docs/*", "win32/*", "apple/toolbox/*",
                     "tools/ci/retro68_*", ".github/workflows/*.yml"),
    },
    "win32": {
        "workflow": ".github/workflows/windows.yml",
        "run": ("common/*", "win32/*", "example/*", "tests/*",
                "cmake/*", "CMakeLists.txt"),
        "may_skip": DOCS_ONLY + ("docs/*", "apple/*", "tools/ci/retro68_*",
                     ".github/workflows/*.yml"),
    },
}


# Linux jobs share their inputs, including host-compiled Toolbox sources.
JOBS["linux-headless"] = {
    "workflow": ".github/workflows/linux.yml",
    "run": ("common/*", "tests/*", "example/*", "apple/toolbox/*", "tools/*",
            "cmake/*", "CMakeLists.txt", "CMakePresets.json", "scripts/*"),
    "may_skip": DOCS_ONLY + ("docs/*", "win32/*", "apple/macos/*"),
}
JOBS["linux-asan"] = JOBS["linux-headless"]
JOBS["linux-release"] = JOBS["linux-headless"]


# Both Toolbox jobs build the same inputs; keep their policy in one place.
JOBS["toolbox-ppc"] = JOBS["toolbox-build"]


SHARED_TEST_SOURCES = Path(__file__).resolve().parents[2] / "cmake" / "LokaTestSources.cmake"


def shared_test_inputs(cmake_file=SHARED_TEST_SOURCES):
    """Paths listed in LOKA_SHARED_TEST_SOURCES (plus their sibling headers).

    These translation units compile into every platform's test executable, so a
    change to one of them must run every job even when its directory is
    otherwise skippable for that job (apple/toolbox/* for macOS and Win32).
    """
    inputs = set()
    try:
        text = cmake_file.read_text()
    except OSError:
        return inputs
    marker = "${_LOKA_TEST_SOURCE_ROOT}/"
    for line in text.splitlines():
        line = line.strip()
        if not line.startswith(marker):
            continue
        rel = line[len(marker):].rstrip(")").strip()
        inputs.add(rel)
        for ext in (".cpp", ".mm", ".c"):
            if rel.endswith(ext):
                inputs.add(rel[: -len(ext)] + ".hpp")
                inputs.add(rel[: -len(ext)] + ".h")
    return inputs


def tested_documentation_inputs(root=Path(__file__).resolve().parents[2]):
    """Derive Linux check inputs from literal docs paths, including split paths.

    For scripts joining "docs" to a filename variable, conservatively include
    their literal Markdown filenames. The classifier's own tests contain path
    examples, not documentation reads, and must not create self-dependencies.
    """
    inputs = set()
    # gen_*.py: a checker may import its output inventory from the generator
    # (check_style_vocab.py reads gen_style_vocab.OUTPUTS), so the literal docs
    # path lives there and the scanner follows it.
    for pattern in ("tests/scripts/*.py", "tests/scripts/*.sh", "tools/ci/check_*.py",
                    "tools/ci/gen_*.py"):
        for script in root.glob(pattern):
            if script == root / "tests/scripts/CiPathsTest.py":
                continue
            text = script.read_text()
            inputs.update(re.findall(r"[\"'](docs/[^\"'\\\s]+)[\"']", text))
            if re.search(r"[\"']docs[\"']\s*/", text):
                inputs.update("docs/" + name for name in
                              re.findall(r"[\"']([^/\"'\\\s]+\.md)[\"']", text))
    return inputs


def classify(job, paths, shared=None):
    """Return (run, reason) for a complete diff; never infer safety by suffix."""
    policy = JOBS[job]
    if not paths:
        return True, "empty diff; run conservatively"
    if policy["workflow"] in paths:
        return True, "own workflow " + policy["workflow"]
    if shared is None:
        shared = shared_test_inputs()
    tested_docs = tested_documentation_inputs() if job.startswith("linux-") else set()
    skipped = set()
    for path in paths:
        if path in tested_docs:
            return True, json.dumps(path, ensure_ascii=True) + " is a tested documentation input"
        if path in shared:
            return True, json.dumps(path, ensure_ascii=True) + " is a shared test source (cmake/LokaTestSources.cmake)"
        match = next((glob for glob in policy["may_skip"]
                      if ("/" in glob or "/" not in path)
                      and fnmatchcase(path, glob)), None)
        if match is not None:
            skipped.add(match)
            continue
        match = next((glob for glob in policy["run"]
                      if fnmatchcase(path, glob)), None)
        # Quote path data so CLI/GITHUB_OUTPUT records always stay on one line.
        reason = json.dumps(path, ensure_ascii=True)
        if match is not None:
            return True, reason + " matches must-run " + match
        return True, reason + " is outside may-skip paths; run conservatively"
    return False, "all changed paths match may-skip: " + ", ".join(sorted(skipped))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--job", action="append", choices=JOBS, required=True)
    parser.add_argument("--summary", type=Path,
                        help="append one summary line per skipped job")
    parser.add_argument("paths", nargs="*", help="paths; default: newline stdin")
    args = parser.parse_args()
    paths = args.paths if args.paths else sys.stdin.read().splitlines()
    for job in args.job:
        run, reason = classify(job, paths)
        print("job=" + job)
        print("run=" + str(run).lower())
        print("reason=" + reason)
        if not run and args.summary is not None:
            with args.summary.open("a", encoding="utf-8") as summary:
                summary.write(job + " skipped: " + reason + "\n")


if __name__ == "__main__":
    main()
