"""Pure unit tests: no fork, prctl, sockets, or live process signalling."""
import os
from pathlib import Path
import signal
import tempfile
import unittest
from unittest.mock import patch
import supervisor as s


class SupervisorTests(unittest.TestCase):
    def test_exit_table(self):
        for code in (0, 1, 3, 77):
            self.assertEqual(s.exit_code(code << 8), code)
            self.assertEqual(s.exit_code(code << 8, True), 1 if code in (0, 77) else code)
            self.assertEqual(s.exit_code(code << 8, False, True), 1 if code in (0, 77) else code)
        self.assertEqual(s.exit_code(signal.SIGKILL), 137)

    def test_children_and_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            childfile = root / '10/task/10/children'
            childfile.parent.mkdir(parents=True)
            childfile.write_text('11 12')
            self.assertEqual(s.direct_children(10, root), [11, 12])
            childfile.unlink()
            for pid, parent in ((10, 1), (11, 10), (12, 11), (13, 1)):
                (root / str(pid)).mkdir(exist_ok=True)
                (root / str(pid) / 'stat').write_text('%d (name with ) spaces) S %d 0' % (pid, parent))
            self.assertEqual(s.direct_children(10, root), [11])

    def test_relay_once_and_fixed_deadline(self):
        relay = s.Relay()
        with patch.object(s.time, 'monotonic', return_value=1), patch.object(s, 'send') as send:
            relay.record(signal.SIGTERM, None)
            relay.deliver(9, 1)
            relay.record(signal.SIGINT, None)
            relay.deliver(9, 20.99)
            self.assertEqual(send.call_count, 1)
            relay.deliver(9, 21)
            relay.deliver(9, 30)
            relay.deliver(None, 40)
            self.assertEqual(send.call_args_list, [((9, signal.SIGTERM),), ((9, signal.SIGKILL),)])

    def run_loop(self, wait, reports, children=(), clock=None, fork=20, create=None):
        now = [0]
        def sleep(_):
            now[0] += 1
        with patch.object(s, 'create_run_root', return_value=Path('/fixture/run'), side_effect=create), patch.object(s, 'remove_run_root') as removal, patch.object(s, 'enable_subreaper'), patch.object(s, 'probe', return_value=8), \
             patch.object(s.signal, 'signal'), patch.object(s.os, 'getpid', return_value=10), \
             patch.object(s.os, 'fork', side_effect=fork if isinstance(fork, Exception) else None, return_value=fork), patch.object(s.os, 'close'), \
             patch.object(s.os, 'read', side_effect=reports), patch.object(s.os, 'waitpid', side_effect=wait), \
             patch.object(s.time, 'monotonic', side_effect=lambda: now[0]), patch.object(s.time, 'sleep', side_effect=sleep), \
             patch.object(s, 'direct_children', side_effect=children), patch.object(s.Path, 'read_text', return_value='fixture'), \
             patch.object(s, 'send') as send:
            try:
                return s.supervise(['unused']), send.call_args_list
            finally:
                self.removal_calls = removal.call_args_list

    def test_selfcheck_and_grace(self):
        code, sends = self.run_loop([(11, 0), (12, 9), ChildProcessError(), (20, 0),
                                    (0, 0), ChildProcessError()], [b'12 10\n'])
        self.assertEqual(code, 0)
        self.assertEqual(sends, [((12, signal.SIGKILL),)])

    def test_only_direct_children_then_adopted_grandchild(self):
        code, sends = self.run_loop([(11, 0), (12, 9), ChildProcessError(), (20, 0), (0, 0),
                                    (0, 0), (30, 9), (0, 0), (31, 9), ChildProcessError()],
                                   [b'12 10\n'], [[30], [31]])
        self.assertEqual(code, 1)
        self.assertEqual(sends, [((12, signal.SIGKILL),), ((30, signal.SIGKILL),), ((31, signal.SIGKILL),)])

    def test_residual_report_once_until_reaped(self):
        with patch('builtins.print') as output:
            code, sends = self.run_loop(
                [(11, 0), (12, 9), ChildProcessError(), (20, 0), (0, 0),
                 (0, 0), (0, 0), (30, 9), ChildProcessError()],
                [b'12 10\n'], [[30], [30]])
        self.assertEqual(code, 1)
        self.assertEqual(sum('[residual]' in call.args[0] for call in output.call_args_list), 1)
        self.assertEqual(sends.count(((30, signal.SIGKILL),)), 2)

    def test_selfcheck_refuses_without_adoption(self):
        with self.assertRaisesRegex(RuntimeError, 'self-check failed'):
            self.run_loop([(0, 0)] * 10 + [ChildProcessError()], [b'12 11\n'] * 6)

    def test_cleanup_exit_codes_and_retention(self):
        for status in (0, 77 << 8, 3 << 8, signal.SIGKILL):
            for empty in (False, True):
                with tempfile.TemporaryDirectory() as tmp:
                    root = Path(tmp) / 'run'
                    root.mkdir()
                    with patch.object(s, 'remove_run_root', side_effect=OSError('refused')) as remove:
                        code = s.finish(root, status, empty=empty)
                    self.assertEqual(remove.call_count, int(empty))
                    self.assertTrue(root.exists())
                    self.assertEqual(code, 1 if status in (0, 77 << 8) else s.exit_code(status))

    def test_cleanup_removes_only_after_echild(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'run'
            root.mkdir()
            (root / 'build-output').write_text('fixture')
            self.assertEqual(s.finish(root, 0, empty=True), 0)
            self.assertFalse(root.exists())
            self.assertTrue(Path(tmp).exists())

    def test_removal_timeout_disarms_alarm_and_retains(self):
        with patch.object(s.signal, 'signal') as handler, patch.object(s.signal, 'setitimer') as timer:
            def expire(_):
                handler.call_args_list[0].args[1](None, None)
            with patch.object(s.shutil, 'rmtree', side_effect=expire):
                self.assertEqual(s.finish(Path('/fixture/run'), 0, empty=True), 1)
            self.assertEqual(timer.call_args_list[0].args, (s.signal.ITIMER_REAL, 30))
            self.assertEqual(timer.call_args_list[-1].args, (s.signal.ITIMER_REAL, 0))

    def test_create_checks_and_failure(self):
        with patch.object(s.Path, 'mkdir'), patch.object(s.Path, 'resolve', autospec=True, side_effect=lambda p: p), \
             patch.object(s.tempfile, 'mkdtemp', side_effect=OSError('creation failed')):
            with self.assertRaisesRegex(OSError, 'creation failed'):
                s.create_run_root()
        with patch.object(s.Path, 'mkdir'), patch.object(s.Path, 'resolve', return_value=Path('/redirected/a/b/c/d/e')):
            with self.assertRaisesRegex(RuntimeError, 'symlink'):
                s.create_run_root()

    def test_flow_only_subreaper_failure_is_not_skip(self):
        with patch.dict(os.environ, {'LOKA_REQUIRE_VSCODE_FLOW': '1'}, clear=True), \
             patch.object(s, 'supervise', side_effect=s.PrerequisiteError('subreaper absent')):
            self.assertEqual(s.main(), 1)

    def test_launcher_failed_or_killed_is_collected_before_removal(self):
        for status, expected in ((1 << 8, 1), (signal.SIGKILL, 137)):
            code, _ = self.run_loop([(11, 0), (12, 9), ChildProcessError(), (20, status),
                                     (0, 0), ChildProcessError()], [b'12 10\n'])
            self.assertEqual(code, expected)

    def test_creation_and_launcher_start_failure_paths(self):
        waits = [(11, 0), (12, 9), ChildProcessError()]
        with self.assertRaisesRegex(OSError, 'creation failed'):
            self.run_loop(waits, [b'12 10\n'], create=OSError('creation failed'))
        self.assertEqual(self.removal_calls, [])
        with self.assertRaisesRegex(OSError, 'fork failed'):
            self.run_loop(waits, [b'12 10\n'], fork=OSError('fork failed'))
        self.assertEqual(len(self.removal_calls), 1)

    def test_collection_deadline_never_removes_tree(self):
        waits = [(11, 0), (12, 9), ChildProcessError(), (20, 3 << 8)] + [(0, 0)] * 20
        code, _ = self.run_loop(waits, [b'12 10\n'], children=[[30]] * 10)
        self.assertEqual(code, 3)
        self.assertEqual(self.removal_calls, [])


if __name__ == '__main__':
    unittest.main()
