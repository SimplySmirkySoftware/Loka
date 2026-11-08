#!/usr/bin/env python3
"""Run text mutation 3; worktree path is argv[1]."""
from pathlib import Path
import subprocess
import sys
raise SystemExit(subprocess.call([sys.executable, str(Path(__file__).with_name('text-run.py')), sys.argv[1], '3']))
