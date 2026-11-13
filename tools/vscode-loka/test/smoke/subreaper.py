"""Test-only init: reap orphaned fake descendants even in containers with no init.

The owner under test cannot call this reaper or ask it to kill processes. Tests
independently kill residual controls after checking the owner's failure report.
"""
import ctypes
import os
import signal
import subprocess
import sys
import time

libc = ctypes.CDLL(None, use_errno=True)
if libc.prctl(36, 1, 0, 0, 0) != 0:  # PR_SET_CHILD_SUBREAPER
    raise OSError(ctypes.get_errno(), 'PR_SET_CHILD_SUBREAPER')
stopping = False


def stop(signum, frame):
    global stopping
    stopping = True


signal.signal(signal.SIGTERM, stop)
child = subprocess.Popen(sys.argv[1:])
# Dedicated launch channel, before the driver can publish readiness.
os.write(3, (str(child.pid) + '\n').encode())
os.close(3)
status = None
while True:
    if stopping:
        # Reap and repeat: killing a parent adopts its previously unrecorded children.
        with open('/proc/self/task/%d/children' % os.getpid()) as children:
            for descendant in children.read().split():
                try:
                    os.kill(int(descendant), signal.SIGKILL)
                except ProcessLookupError:
                    pass
    try:
        pid, result = os.waitpid(-1, os.WNOHANG)
    except ChildProcessError:
        break
    if pid == child.pid:
        status = os.waitstatus_to_exitcode(result)
    if pid == 0:
        time.sleep(0.01)
sys.exit(status if status is not None else 1)
