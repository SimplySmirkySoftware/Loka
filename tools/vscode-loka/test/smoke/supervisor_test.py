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
            self.assertEqual(s.exit_code(code << 8, False, True), 1)
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

    def run_loop(self, wait, reports, children=(), clock=None):
        now = [0]
        def sleep(_):
            now[0] += 1
        with patch.object(s, 'enable_subreaper'), patch.object(s, 'probe', return_value=8), \
             patch.object(s.signal, 'signal'), patch.object(s.os, 'getpid', return_value=10), \
             patch.object(s.os, 'fork', return_value=20), patch.object(s.os, 'close'), \
             patch.object(s.os, 'read', side_effect=reports), patch.object(s.os, 'waitpid', side_effect=wait), \
             patch.object(s.time, 'monotonic', side_effect=lambda: now[0]), patch.object(s.time, 'sleep', side_effect=sleep), \
             patch.object(s, 'direct_children', side_effect=children), patch.object(s.Path, 'read_text', return_value='fixture'), \
             patch.object(s, 'send') as send:
            return s.supervise(['unused']), send.call_args_list

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


if __name__ == '__main__':
    unittest.main()
