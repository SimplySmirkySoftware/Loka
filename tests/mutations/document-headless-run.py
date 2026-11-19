#!/usr/bin/env python3
"""Run the headless document mutations independently. Build failures are not kills."""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "common/app/scene/detail/OneShotComposition.hpp"
MUTATIONS = [
    ("mount-scope", "loka::core::NextTickTracker::RunScope run(scene.nextTickTracker_);", "", "Branches"),
    ("attach-publication", "scene.updateLifecycle(ON_ATTACH);", "", "Lifecycle"),
    ("detach-publication", "scene.updateLifecycle(ON_DETACH);", "", "Lifecycle"),
    ("flush-admission", "              accepted = admissible(scene);", "", "UpdateRefusal"),
    ("mount-admission", "mounted && admissible(scene)", "mounted", "MountRefusal"),
    ("limit-99", "runs < 100", "runs < 99", "LastFlush"),
    ("limit-101", "runs < 100", "runs < 101", "Exhaustion"),
    ("root-definition", "if (!scene.getRootDefinition())", "if (false)", "Clone"),
    ("failure-detach", "scene.updateAttached(false);", "if (!accepted) return false;\n            scene.updateAttached(false);", "WriterRefusal"),
]


def run(command, log):
    with log.open("wb") as output:
        return subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode


def mutation(entry):
    name, old, new, suffix = entry
    original = HEADER.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, "anchor mismatch: " + name
    logs = ROOT / "build/document-headless-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    test = "testDocumentHeadless" + suffix

    def check(phase, green):
        if run(["cmake", "--build", "build/Testing", "--target", "LokaContractTests", "-j", "4"],
               logs / f"{name}-{phase}-build.log"):
            raise RuntimeError(f"{name} {phase}: build failure")
        status = run(["build/Testing/LokaContractTests", test], logs / f"{name}-{phase}.log")
        if (status == 0) != green:
            raise RuntimeError(f"{name} {phase}: unexpected {test} exit {status}")

    check("baseline", True)
    try:
        HEADER.write_bytes(source.replace(old, new).encode())
        check("mutant", False)
        print(f"killed {name}: {test}", flush=True)
    finally:
        HEADER.write_bytes(original)
        check("restored", True)
        assert HEADER.read_bytes() == original


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in MUTATIONS])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    for entry in MUTATIONS:
        if args.mutation in ("all", entry[0]):
            mutation(entry)
