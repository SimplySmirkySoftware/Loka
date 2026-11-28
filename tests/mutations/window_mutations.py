"""Build each window mutant and require its named assertion before restoring the source."""

import argparse
from pathlib import Path
import shlex
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(command, log):
    with log.open("wb") as output:
        return subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT).returncode


def mutation(entry, build, test, logs):
    name, filename, old, new, pin, expected, *other_edits = entry
    path = ROOT / filename
    original = path.read_bytes()
    source = original.decode()
    for old, new in [(old, new), *other_edits]:
        if source.count(old) != 1:
            raise ValueError("anchor mismatch: " + name)
        source = source.replace(old, new)

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
        path.write_bytes(source.encode())
        check("mutant")
    finally:
        path.write_bytes(original)
        if path.read_bytes() != original:
            raise RuntimeError("restoration failed: " + name)
        check("restored")
    print(f"killed {name}: {pin} ({expected})", flush=True)


def main(mutations, log_dir, description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--build", default="cmake --build --preset testing --target LokaContractTests LokaScenarioTests -j8")
    parser.add_argument("--test", default="ctest --preset testing --output-on-failure --no-tests=error -R", help="command to which the pin name is appended")
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in mutations])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    logs = ROOT / "build" / log_dir
    logs.mkdir(parents=True, exist_ok=True)
    for entry in mutations:
        if args.mutation in ("all", entry[0]):
            mutation(entry, shlex.split(args.build), shlex.split(args.test), logs)
