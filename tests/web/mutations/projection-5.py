#!/usr/bin/env python3
import os
from pathlib import Path
import sys
os.execv(sys.executable, [sys.executable, str(Path(__file__).with_name('projection-run.py')), sys.argv[1], '5'])
