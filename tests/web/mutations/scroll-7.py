#!/usr/bin/env python3
import runpy
import sys
from pathlib import Path
sys.argv.append('7')
runpy.run_path(str(Path(__file__).with_name('scroll-run.py')), run_name='__main__')
