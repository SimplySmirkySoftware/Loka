#!/usr/bin/env python3
"""Fail when committed card-shadow outputs differ from the generator."""

import difflib
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = (
    Path("common/app/style/CardShadowTables.hpp"),
    Path("common/app/style/CardShadowTables.cpp"),
)


def main():
    with tempfile.TemporaryDirectory(prefix="loka-card-shadow-") as temp:
        output_root = Path(temp)
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/ci/gen_card_shadow_tables.py"),
             "--output-root", str(output_root)],
            cwd=str(ROOT),
            check=False,
        )
        if result.returncode:
            return result.returncode
        stale = False
        for relative in OUTPUTS:
            committed = ROOT / relative
            generated = output_root / relative
            before = committed.read_text(encoding="utf-8") if committed.exists() else ""
            after = generated.read_text(encoding="utf-8")
            if before == after:
                continue
            stale = True
            sys.stderr.writelines(difflib.unified_diff(
                before.splitlines(True), after.splitlines(True),
                fromfile=str(relative), tofile="generated/%s" % relative,
            ))
        if stale:
            print("card shadow tables is stale; run tools/ci/gen_card_shadow_tables.py", file=sys.stderr)
            return 1
    print("card shadow tables outputs are current")
    return 0


if __name__ == "__main__":
    sys.exit(main())
