#!/usr/bin/env python3
"""Both generator entry points reject invalid input before producing a case."""
import ctypes
from pathlib import Path
import subprocess
import sys


def main():
    runner = sys.argv[1]
    wrapper = Path(__file__).with_name("DocumentMarkdownParseBack.py")
    maximum = ctypes.c_uint(-1).value
    for command in ([runner, "--document-markdown-cases"], [sys.executable, "-S", wrapper, runner]):
        for bad in ("", "-1", "xyz", "2x", " 1", str(maximum + 1), "9" * 100):
            for args in ([bad, "0"], ["0", bad]):
                result = subprocess.run(command + args, capture_output=True, timeout=5)
                if result.returncode != 2 or result.stdout or b"usage:" not in result.stderr.lower():
                    raise AssertionError((args, result.returncode, result.stderr))
    subprocess.run([runner, "--document-markdown-cases", str(maximum), "000"], check=True)


if __name__ == "__main__":
    main()
