#!/usr/bin/env python3
"""Drop each stored-child clone check and run its discriminating contract pin."""

import argparse
from pathlib import Path
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SHOW = "common/app/nodes/nestable/Show.hpp"
TRUE_COUNT = "copy->trueBranch_.childrenCount() != this->trueBranch_.childrenCount()"
FALSE_COUNT = "copy->falseBranch_.childrenCount() != this->falseBranch_.childrenCount()"
SHOW_CHECK = TRUE_COUNT + "\n            || " + FALSE_COUNT

# The LimitedCloneProbe budget is spent inside child clone(), after the
# outer nothrow allocation. It cannot inject a null candidate allocation.
MUTATIONS = [
    ("show-true", SHOW, SHOW_CHECK, FALSE_COUNT,
     "testShowCloneRefusalPreservesDeclaredRoot", "composition.root() == old"),
    ("show-false", SHOW, SHOW_CHECK, TRUE_COUNT,
     "testShowCloneRefusesSecondArm", "!copy.isSet()"),
    ("scope", "common/app/nodes/nestable/PolicyScope.hpp",
     "copy->content_.childrenCount() != this->content_.childrenCount()", "false",
     "testPolicyScopeCloneRefusesSecondChild", "!copy.isSet()"),
    ("probe-seat", "common/testing/scene/ProbeArmSeatDefinition.hpp",
     "this->arms_[arm].isSet() && !copy->arms_[arm].isSet()", "false",
     "testProbeArmSeatCloneRefusesOccupiedSlot", "!copy.isSet()"),
    ("retained-fragment", "tests/support/LocalRebuildRefusal.hpp",
     "!copy.isSet() || copy->childrenCount() != this->childrenCount()", "!copy.isSet()",
     "testRetainedFragmentCloneRefusesChild", "!copy.isSet()"),
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
    parser.add_argument("--build", default="cmake --build --preset testing --target LokaContractTests -j8")
    parser.add_argument("--test", default="build/Testing/LokaContractTests", help="command to which the pin name is appended")
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in MUTATIONS])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    logs = ROOT / "build/wrapper-clone-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    for entry in MUTATIONS:
        if args.mutation in ("all", entry[0]):
            mutation(entry, shlex.split(args.build), shlex.split(args.test), logs)
