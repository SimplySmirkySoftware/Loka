#!/usr/bin/env python3
"""Mutate each window declaration refusal transition and verify its contract pin."""

import argparse
from pathlib import Path
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WINDOW = "common/app/core/Window.cpp"
DEFINITION = "common/app/core/WindowDefinition.hpp"
VEHICLE = "tests/standalone/StandaloneScenarioSupport.cpp"
BLOCKED = "definition.create(&context) == 0"
VERDICT = """      const bool awaitingVerdict =
          this->borrowedApp_ && !this->completed_ && this->completionMode_ != REARM_COMPLETED_SCENE;
      return this->mountFailed_ || awaitingVerdict;"""

MUTATIONS = [
    ("copy-drops-refused", WINDOW,
     "source.refused() || (source.owned() && !this->owned())", "source.owned() && !this->owned()",
     "testWindowRootCaptureRefusal", BLOCKED),
    ("copy-loses-root", WINDOW,
     "source.refused() || (source.owned() && !this->owned())", "source.refused()",
     "testWindowRootCopyRefusal", BLOCKED),
    ("assignment-keeps-empty", WINDOW,
     "if (!this->root_.tryAssign(rhs.root_))\n  {\n    this->refuseContentWrite();",
     "if (!this->root_.tryAssign(rhs.root_))\n  {",
     "testWindowRootAssignmentRefusal", BLOCKED),
    ("assignment-reclones", WINDOW, "this->definition_.reset(candidate.take());",
     "this->definition_.reset(candidate.owned() ? candidate.definition_->clone() : 0);",
     "testWindowRootSuccessfulCopies", "g_limitedCloneCalls == 1"),
    ("capture-keeps-empty", WINDOW, "if (!candidate.isSet())\n  {\n    this->refuseContentWrite();",
     "if (!candidate.isSet())\n  {", "testWindowRootCaptureRefusal", BLOCKED),
    ("accept-null-root-scene", WINDOW, "if (scene->getRootDefinition())", "if (scene)",
     "testWindowSceneHandoffRefusal", "!props.peekInitialScene()"),
    ("release-before-allocation", WINDOW, "      candidate = loka::core::LokaNew<InitialSceneHandoff>(handoffSite(), scene);",
     "    {\n      this->releaseInitialSceneHandoff();\n      candidate = loka::core::LokaNew<InitialSceneHandoff>(handoffSite(), scene);\n    }",
     "testWindowSceneHandoffRefusal", "target.peekInitialScene() == pending"),
    ("clone-ignores-refusal", DEFINITION, "copy.isSet() && !copy->props.rootDefinitionRefused()",
     "copy.isSet()", "testWindowRootCloneRefusal", "!copy.isSet()"),
    ("create-ignores-refusal", DEFINITION, "    if (resolved.rootDefinitionRefused())\n      return 0;\n",
     "", "testWindowRootResolutionRefusal", BLOCKED),
    ("no-verdict-succeeds", VEHICLE, VERDICT, "      return this->mountFailed_;",
     "testScrapbookStandaloneDeclarationRefusalIsWindowless", "config.exitCode() != 0"),
]


def run(command, log):
    with log.open("wb") as output:
        return subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode


def mutation(entry, build, test, logs):
    name, filename, old, new, pin, expected = entry
    path = ROOT / filename
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, "anchor mismatch: " + name

    def check(phase):
        build_log = logs / f"{name}-{phase}-build.log"
        if run(build, build_log):
            raise RuntimeError(f"{name}: build failed, see {build_log}")
        log = logs / f"{name}-{phase}.log"
        status = run(test + [pin], log)
        if phase == "mutant":
            if status == 0 or "LOKA_VERIFY failed: " + expected not in log.read_text():
                raise RuntimeError(f"{name}: missing expected failure, see {log}")
        elif status:
            raise RuntimeError(f"{name}: {phase} failed, see {log}")

    check("baseline")
    try:
        path.write_bytes(source.replace(old, new).encode())
        check("mutant")
    finally:
        path.write_bytes(original)
        assert path.read_bytes() == original
        check("restored")
    print(f"killed {name}: {pin} ({expected})", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="cmake --build --preset testing --target LokaContractTests LokaScenarioTests -j8")
    parser.add_argument("--test", default="ctest --preset testing --output-on-failure --no-tests=error -R", help="command to which the pin name is appended")
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in MUTATIONS])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    logs = ROOT / "build/window-root-refusal-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    for entry in MUTATIONS:
        if args.mutation in ("all", entry[0]):
            mutation(entry, shlex.split(args.build), shlex.split(args.test), logs)
