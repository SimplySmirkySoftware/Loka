"""Linux smoke process owner. Only this loop reaps; only unreaped children are signalled."""
import ctypes
import os
from pathlib import Path
import shutil
import tempfile
import signal
import sys
import time


def direct_children(pid, proc=Path('/proc')):
    """One cleanup pass: own kernel child list, or a PPID-filtered proc snapshot."""
    try:
        return [int(p) for p in (proc / str(pid) / 'task' / str(pid) / 'children').read_text().split()]
    except FileNotFoundError:
        children = []
        for entry in proc.iterdir():
            if not entry.name.isdigit():
                continue
            try:
                fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
                if int(fields[1]) == pid:
                    children.append(int(entry.name))
            except (FileNotFoundError, ProcessLookupError):
                pass
        return children


def send(pid, sig):
    try:
        os.kill(pid, sig)
    except ProcessLookupError:
        pass


def exit_code(status, leftovers=False, incomplete=False):
    code = os.waitstatus_to_exitcode(status)
    if code < 0:
        code = 128 - code
    if (incomplete or leftovers) and code in (0, 77):
        return 1
    return code


def create_run_root():
    """One supervisor-owned tree; reject redirected build parents before allocating it."""
    repo = Path(__file__).resolve().parents[4]
    root = repo / 'build'
    for directory in (root, root / 'vscode-smoke'):
        directory.mkdir(parents=True, exist_ok=True)
        if directory.resolve() != directory:
            raise RuntimeError('Smoke build/run root must not be a symlink: ' + str(directory))
    return Path(tempfile.mkdtemp(prefix='run-', dir=root / 'vscode-smoke'))


def remove_run_root(root):
    """Cost: once after ECHILD; visit the run tree only, with a 30-second deadline.

    The main process removes synchronously: no cleanup child can outlive the deadline.
    SIGALRM interrupts blocking filesystem operations as well as the directory walk.
    """
    def expired(_signal, _frame):
        raise TimeoutError('Run directory removal timed out (30 s)')
    previous = signal.signal(signal.SIGALRM, expired)
    try:
        signal.setitimer(signal.ITIMER_REAL, 30)
        shutil.rmtree(root)
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, previous)


def finish(root, status, leftovers=False, *, empty):
    """Only ECHILD licenses deletion; cleanup cannot turn an existing failure into success."""
    if root is None:
        return exit_code(status, leftovers, not empty)
    try:
        if not empty:
            raise RuntimeError('supervisor could not reach ECHILD')
        remove_run_root(root)
    except (OSError, RuntimeError) as error:
        print('[fail] run directory retained: %s: %s' % (root, error), file=sys.stderr)
        return exit_code(status, leftovers, True)
    return exit_code(status, leftovers)


class PrerequisiteError(RuntimeError):
    pass


def enable_subreaper():
    if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0):
        raise OSError(ctypes.get_errno(), 'PR_SET_CHILD_SUBREAPER')


def probe():
    """No PID/name search: grandchild reports its own identity and parent on a pipe."""
    read, write = os.pipe()
    child = os.fork()
    if child == 0:
        os.close(read)
        if os.fork() != 0:
            os._exit(0)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, signal.SIG_DFL)
        try:
            while True:
                os.write(write, ('%d %d\n' % (os.getpid(), os.getppid())).encode())
                time.sleep(.01)
        except BrokenPipeError:
            os._exit(0)
    os.close(write)
    os.set_blocking(read, False)
    return read


class Relay:
    """Handler records the first request; the main loop alone sends and escalates."""
    def __init__(self):
        self.request = None
        self.sent = False
        self.killed = False

    def record(self, signum, _frame):
        if self.request is None:
            self.request = (signum, time.monotonic())

    def requested(self):
        return self.request is not None

    def deliver(self, launcher, now):
        if launcher is None or self.request is None:
            return
        if not self.sent:
            send(launcher, self.request[0])
            self.sent = True
        if not self.killed and now >= self.request[1] + 20:
            send(launcher, signal.SIGKILL)
            self.killed = True


def supervise(argv):
    if sys.platform != 'linux':
        raise PrerequisiteError('Linux required')
    relay = Relay()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, relay.record)
    try:
        enable_subreaper()
        pipe = probe()
    except OSError as error:
        raise PrerequisiteError(str(error)) from error
    pending = b''
    verified = False
    phase = 'probe'
    deadline = time.monotonic() + 5
    launcher = None
    status = None
    leftovers = False
    reported = set()
    root = None
    try:
        while True:
            now = time.monotonic()
            if phase == 'probe':
                try:
                    pending += os.read(pipe, 4096)
                except BlockingIOError:
                    pass
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    pid, parent = map(int, line.split())
                    if parent == os.getpid() and not verified:
                        send(pid, signal.SIGKILL)
                        verified = True
                if now >= deadline or relay.requested():
                    os.close(pipe)
                    pipe = None
                    phase = 'probe-failed'
                    deadline = now + 5
            elif phase == 'running':
                relay.deliver(launcher, now)
            elif phase == 'grace' and now >= deadline:
                phase = 'collect'
                deadline = now + 5
                leftovers = True
            if phase == 'collect':
                # No wait between enumeration and the entire send batch. Unreaped PIDs stay ours.
                for pid in direct_children(os.getpid()):
                    try:
                        name = Path('/proc/%d/comm' % pid).read_text().strip()
                    except FileNotFoundError:
                        name = '<exited>'
                    if pid not in reported:
                        print('[residual] pid=%d name=%s' % (pid, name), flush=True)
                        reported.add(pid)
                    send(pid, signal.SIGKILL)
            empty = False
            # The sole reaping authority, shared by probe, launch, grace and collection.
            while True:
                try:
                    pid, result = os.waitpid(-1, os.WNOHANG)
                except ChildProcessError:
                    empty = True
                    break
                if pid == 0:
                    break
                reported.discard(pid)
                if pid == launcher:
                    launcher = None
                    status = result
                    phase = 'grace'
                    deadline = time.monotonic() + 2
            if phase == 'probe' and verified and empty:
                os.close(pipe)
                pipe = None
                root = create_run_root()
                try:
                    launcher = os.fork()
                except OSError:
                    finish(root, 1 << 8, empty=empty)
                    raise
                if launcher == 0:
                    for sig in (signal.SIGINT, signal.SIGTERM):
                        signal.signal(sig, signal.SIG_DFL)
                    try:
                        os.execvpe(argv[0], argv, {**os.environ, 'LOKA_SMOKE_RUN_ROOT': str(root)})
                    except OSError as error:
                        print('[fail] launcher exec: ' + str(error), file=sys.stderr)
                        os._exit(1)
                phase = 'running'
            elif phase == 'probe-failed' and (empty or now >= deadline):
                raise PrerequisiteError('subreaper self-check failed')
            elif phase in ('grace', 'collect') and empty:
                return finish(root, status, leftovers, empty=empty)
            elif phase == 'collect' and now >= deadline:
                print('[fail] supervisor could not reach ECHILD', file=sys.stderr)
                return finish(root, status, leftovers, empty=False)
            time.sleep(.01)
    finally:
        if pipe is not None:
            os.close(pipe)


def main():
    try:
        return supervise(sys.argv[1:])
    except PrerequisiteError as error:
        required = any(os.environ.get(k) == '1' for k in
                       ('LOKA_REQUIRE_VSCODE_FLOW', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME', 'LOKA_REQUIRE_SMOKE_PROCESS_TESTS'))
        print('[%s] smoke supervisor prerequisite: %s' % ('fail' if required else 'skip', error), file=sys.stderr)
        return 1 if required else 77
    except (OSError, RuntimeError) as error:
        print('[fail] supervisor: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
