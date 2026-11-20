"""Build, run and restore each document mutation independently."""
import argparse
import signal
import subprocess


def run(root, command, log):
    with log.open("wb") as output:
        return subprocess.run(command, cwd=root, stdout=output, stderr=subprocess.STDOUT).returncode


def mutation(root, suite, entry):
    name, header, old, new, suffix, *other_edits = entry
    original = header.read_bytes()
    source = original.decode()
    for old, new in [(old, new), *other_edits]:
        assert source.count(old) == 1, "anchor mismatch: " + name
        source = source.replace(old, new)
    logs = root / "build" / f"document-{suite.lower()}-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    test = "testDocument" + suite + suffix

    def check(phase, green):
        if run(root, ["cmake", "--build", "build/Testing", "--target", "LokaContractTests", "-j", "4"],
               logs / f"{name}-{phase}-build.log"):
            raise RuntimeError(f"{name} {phase}: build failure")
        status = run(root, ["build/Testing/LokaContractTests", test], logs / f"{name}-{phase}.log")
        if (status == 0) != green:
            raise RuntimeError(f"{name} {phase}: unexpected {test} exit {status}")

    check("baseline", True)
    try:
        header.write_bytes(source.encode())
        check("mutant", False)
        print(f"killed {name}: {test}", flush=True)
    finally:
        header.write_bytes(original)
        check("restored", True)
        assert header.read_bytes() == original


def main(root, suite, mutations):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mutation", nargs="?", default="all", choices=["all"] + [m[0] for m in mutations])
    args = parser.parse_args()

    def terminate(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    for entry in mutations:
        if args.mutation in ("all", entry[0]):
            mutation(root, suite, entry)
