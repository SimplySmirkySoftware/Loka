#!/usr/bin/env python3
"""Check defined startup code in unstripped final PPC XCOFF applications.

This is a link wiring pin, not evidence of native dialog/exit behavior.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

SYMBOL = ".loka::toolbox::RequireUnicodeSystem()"


def has_startup(output):
    return any(re.fullmatch(r"\s*[0-9a-fA-F]+\s+[Tt]\s+" + re.escape(SYMBOL) + r"\s*", line)
               for line in output.splitlines())


def check(nm, unicode_app, te_app):
    for path, expected in ((unicode_app, True), (te_app, False)):
        if not path.is_file():
            raise ValueError(f"missing XCOFF: {path}")
        result = subprocess.run([str(nm), "-C", str(path)], capture_output=True, text=True, check=True)
        if has_startup(result.stdout) != expected:
            raise ValueError(f"{path}: defined {SYMBOL} must be {'present' if expected else 'absent'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", default="powerpc-apple-macos-nm")
    parser.add_argument("unicode_app", type=Path)
    parser.add_argument("te_app", type=Path)
    args = parser.parse_args()
    try:
        check(args.nm, args.unicode_app, args.te_app)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print("PPC startup symbol wiring passed (not native runtime evidence)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
