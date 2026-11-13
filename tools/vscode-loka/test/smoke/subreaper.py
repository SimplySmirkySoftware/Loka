"""Test-only init: reap orphaned fake descendants even in containers with no init.

The owner under test cannot call this reaper or ask it to kill processes. Tests
independently kill residual controls after checking the owner's failure report.
"""
import ctypes
import os
import subprocess
import sys
import time

libc = ctypes.CDLL(None, use_errno=True)
if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
    raise OSError(ctypes.get_errno(), 'PR_SET_CHILD_SUBREAPER')
child = subprocess.Popen(sys.argv[1:])
status = None
while True:
    try:
        pid, result = os.waitpid(-1, os.WNOHANG)
    except ChildProcessError:
        break
    if pid == child.pid:
        status = os.waitstatus_to_exitcode(result)
    if pid == 0:
        time.sleep(0.01)
sys.exit(status if status is not None else 1)
