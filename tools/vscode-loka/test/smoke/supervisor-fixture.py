"""Real vehicle controls. The outer production supervisor owns all test orphans.
The observer holds pidfds independently of the supervisor's census/diagnostics.
"""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
import supervisor

HERE = Path(__file__).resolve().parent
mode, variant, root_text = sys.argv[1:4]
root = Path(root_text)


def until(read, seconds=15):
    deadline = time.monotonic() + seconds
    while True:
        value = read()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise AssertionError('Independent watch timed out')
        time.sleep(.02)


def alive(fd):
    return not select.select([fd], [], [], 0)[0]


if mode == 'sleeper':
    until(lambda: (root / 'go').exists())
    time.sleep(.3)
    sys.exit(0)
elif mode == 'payload':
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    if variant == 'stuck':
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    (root / 'launcher').write_text(str(os.getpid()))
    command = [sys.executable, __file__, 'sleeper', variant, str(root)] if variant == 'e' else ['sleep', '61']
    child = subprocess.Popen(command, start_new_session=True, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
    (root / 'watch').write_text(str(child.pid))
    until(lambda: (root / 'go').exists())
    if variant == 'stuck':
        time.sleep(60)
    sys.exit(0)
elif mode == 'inner':
    if variant in ('a', 'b'):
        supervisor.enable_subreaper = lambda: None
    if variant == 'b':
        # Negative control: bypass the adoption proof with an ordinary direct child.
        def bypass_probe():
            read, write = os.pipe()
            pid = os.fork()
            if pid == 0:
                os.close(read)
                os.write(write, ('%d %d\n' % (os.getpid(), os.getppid())).encode())
                time.sleep(60)
                os._exit(0)
            os.close(write)
            os.set_blocking(read, False)
            return read
        supervisor.probe = bypass_probe
    if variant == 'c':
        original = supervisor.send
        first = [True]
        def no_cleanup_kill(pid, sig):
            if first[0]:
                first[0] = False
                original(pid, sig)  # Preserve self-check kill, suppress cleanup kills.
        supervisor.send = no_cleanup_kill
    sys.argv = [str(HERE / 'supervisor.py'), sys.executable, __file__, 'payload', variant, str(root)]
    sys.exit(supervisor.main())
else:
    # This process is itself launched by supervisor.py; no second orphan-reaping implementation.
    inner = subprocess.Popen([sys.executable, __file__, 'inner', variant, str(root)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    handles = []
    try:
        if variant == 'a':
            output = inner.communicate(timeout=15)[0].decode()
            assert inner.returncode == 1, output
            assert 'self-check failed' in output and not (root / 'launcher').exists(), output
        else:
            pid = int(until(lambda: (root / 'watch').read_text() if (root / 'watch').exists() else None))
            handles.append(os.pidfd_open(pid))
            assert alive(handles[0])
            if variant != 'launcher-kill':
                (root / 'go').touch()
            if variant == 'stuck':
                inner.send_signal(signal.SIGTERM)
            if variant == 'launcher-kill':
                time.sleep(float(os.environ.get('LOKA_TEST_WATCH_DELAY', '0')))
                launcher = int((root / 'launcher').read_text())
                fd = os.pidfd_open(launcher)
                signal.pidfd_send_signal(fd, signal.SIGKILL)
                os.close(fd)
            output = inner.communicate(timeout=35)[0].decode()
            if variant in ('b', 'c'):
                assert alive(handles[0]), 'negative control failed to leave a live process'
                assert inner.returncode == (0 if variant == 'b' else 1), output
                if variant == 'c':
                    assert 'could not reach ECHILD' in output, output
            else:
                until(lambda: not alive(handles[0]))
                assert inner.returncode == (0 if variant == 'e' else 137 if variant in ('stuck', 'launcher-kill') else 1), output
                if variant == 'd':
                    assert '[residual]' in output, output
            print('[watch] %s: independent pidfd observation passed' % variant, flush=True)
    finally:
        for fd in handles:
            if alive(fd):
                signal.pidfd_send_signal(fd, signal.SIGKILL)
            os.close(fd)
        if inner.poll() is None:
            inner.terminate()
            inner.communicate(timeout=30)
