#!/usr/bin/env python3
"""Host-only stage-last receipt and runner checks; no emulator is invoked."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class StageLastTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='stage-last-test-')
        self.root = pathlib.Path(self.temp.name)
        for name in ('tests/toolbox/run-scenario.sh', 'scripts/retro68-env.sh', 'scripts/env-file.sh',
                     'scripts/rig/toolbox/scenario_run_provenance.py',
                     'scripts/rig/toolbox/classic_golden_identity.py',
                     'scripts/rig/golden_identity_guard.py', 'scripts/rig/source-tree-stamp.sh',
                     'tests/scenarios/scenarios.txt'):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        self.work = self.root / 'build/mame-scenario/helloworld/startup'
        self.work.mkdir(parents=True)
        self.appl = self.root / 'build/retro68/68k/Release/tests/toolbox/LokaHelloWorldTestsToolbox68K.bin'
        self.appl.parent.mkdir(parents=True)
        self.appl.write_bytes(b'fixture application')
        self.audit = self.work / 'LokaTestsToolbox.audit'
        self.audit.write_text('fixture audit\n')
        self.expected = self.root / 'tests/scenarios/expected/helloworld/startup.audit'
        self.expected.parent.mkdir(parents=True)
        shutil.copyfile(self.audit, self.expected)
        self.capture = self.work / 'startup.png'
        self.capture.write_bytes(b'fixture capture (staging is stubbed)')
        self.receipt = self.work / 'scenario-run-provenance.txt'
        self.helper = self.root / 'scripts/rig/toolbox/scenario_run_provenance.py'
        self.args = ['--provenance', str(self.receipt), '--application', str(self.appl),
                     '--source-tree', str(self.root), '--registry', str(self.root / 'tests/scenarios/scenarios.txt'),
                     '--capture-adapter', 'mame-screen-snapshot.v2', '--mode', 'capture']
        self.command(['python3', str(self.helper), 'record', *self.args])
        self.command(['python3', str(self.helper), 'audit-matched', '--provenance', str(self.receipt), '--audit', str(self.audit)])
        self.command(['python3', str(self.helper), 'capture-ready', '--provenance', str(self.receipt), '--capture', str(self.capture)])

    def tearDown(self):
        self.temp.cleanup()

    def command(self, args):
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def publish_source_stamp(self):
        # The runner refuses a vehicle without a stamp from this source tree
        # (#1802) before disk staging; publish after the fixture's last edit.
        # Like the build: withdraw before compiling, publish after linking.
        for operation in ('withdraw', 'publish'):
            self.command(['bash', str(self.root / 'scripts/rig/source-tree-stamp.sh'), operation,
                          str(self.appl.with_suffix('.source-stamp.txt')), str(self.root)])

    def runner(self, *flags):
        return subprocess.run(['bash', str(self.root / 'tests/toolbox/run-scenario.sh'),
                               'helloworld', 'startup', *flags], capture_output=True, text=True)

    def refuse(self, field):
        result = self.runner('--stage-last')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('provenance stage failed:', result.stderr)
        self.assertIn(field, result.stderr)
        self.assertTrue(self.capture.exists())

    def test_identity_fields(self):
        original = self.receipt.read_text()
        for field in ('application_sha256', 'source_tree_identity', 'registry_sha256', 'capture_adapter', 'mode', 'audit_verdict', 'audit_sha256', 'capture_sha256'):
            with self.subTest(field=field):
                self.receipt.write_text('\n'.join(field + '=changed' if line.startswith(field + '=') else line for line in original.splitlines()) + '\n')
                self.refuse(field)
        self.receipt.write_text(original)

    def test_missing_and_malformed_receipt(self):
        self.receipt.unlink()
        self.refuse('cannot read')
        self.receipt.write_text('not a receipt')
        self.refuse('malformed')

    def test_missing_inputs(self):
        for path, name in ((self.audit, 'audit'), (self.capture, 'capture'), (self.appl, self.appl.name)):
            with self.subTest(name=name):
                content = path.read_bytes()
                path.unlink()
                result = self.runner('--stage-last')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(name, result.stderr)
                path.write_bytes(content)

    def test_changed_capture(self):
        self.capture.write_bytes(b'edited after the attested run')
        self.refuse('capture_sha256 differs from the attested run')

    def test_capture_not_normalized(self):
        self.command(['python3', str(self.helper), 'record', *self.args])
        self.command(['python3', str(self.helper), 'audit-matched', '--provenance', str(self.receipt), '--audit', str(self.audit)])
        self.refuse('capture_sha256 is unset')

    def test_changed_tracked_audit(self):
        self.expected.write_text('changed')
        self.refuse('tracked audit_sha256')

    def test_changed_saved_audit(self):
        self.audit.write_text('changed saved audit')
        self.refuse('Scenario run provenance refused: audit_sha256 differs from the matched run')

    def test_replaced_audit_and_reference(self):
        self.audit.write_text('replacement audit')
        self.expected.write_text('replacement audit')
        self.refuse('audit_sha256 differs from the matched run')

    def test_normal_run_records_before_disk_staging(self):
        # The development-disk stub refuses before the emulator call. No MAME
        # binary is installed or invoked by this fixture.
        identity = self.root / 'scripts/rig/toolbox/classic_golden_identity.py'
        identity.write_text(identity.read_text().replace('sys.exit(main(sys.argv[1:]))', 'sys.exit(0)'))
        (self.root / '.env-mame').write_text('MAME_EXECUTABLE=/bin/false\nMAME_HDA=' + str(self.root / 'template.hd') + '\n')
        (self.root / 'template.hd').write_bytes(b'fixture boot template')
        (self.root / 'tests/toolbox/mame-launch.lua').write_text('-- fixture')
        (self.root / 'tests/toolbox/mame-keys.lua').write_text('-- shared keys fixture')
        disk = self.root / 'scripts/mame-dev-disk.sh'
        disk.write_text('#!/usr/bin/env bash\nexit 88\n')
        disk.chmod(0o755)
        self.publish_source_stamp()
        result = self.runner()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('mame stage failed: development disk creation failed', result.stderr)
        self.assertIn('audit_verdict=not-matched', self.receipt.read_text())
        self.assertFalse(self.capture.exists())
        # A receipt write failure must fail its named stage before disk staging.
        helper = self.helper.read_text()
        self.helper.write_text(helper.replace('run(parser.parse_args())', 'raise OSError("fixture write failure")'))
        self.publish_source_stamp()
        result = self.runner()
        self.assertIn('provenance stage failed:', result.stderr)
        self.assertIn('fixture write failure', result.stderr)

    def test_unmatched_run(self):
        self.command(['python3', str(self.helper), 'record', *self.args])
        self.refuse('audit_verdict')

    def test_exclusive_flags(self):
        for flag in ('--update-golden', '--probe', '--structural-audit'):
            for flags in (('--stage-last', flag), (flag, '--stage-last')):
                result = self.runner(*flags)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('arguments stage failed:', result.stderr)
                self.assertTrue(self.capture.exists())

    def test_matching_receipt_reaches_shared_staging(self):
        # Keep imported identity primitives intact; replace only the CLI entry
        # point in the isolated fixture to inspect real shell argument routing.
        identity = self.root / 'scripts/rig/toolbox/classic_golden_identity.py'
        text = identity.read_text()
        text = text.replace('sys.exit(main(sys.argv[1:]))',
                            'print("STAGING " + " ".join(sys.argv[1:]))')
        identity.write_text(text)
        result = self.runner('--stage-last')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('STAGING stage-capture', result.stdout)
        for value in (str(self.capture), str(self.appl), '--example helloworld', '--scenario startup'):
            self.assertIn(value, result.stdout)
        self.assertTrue(self.capture.exists())

    def test_changed_current_inputs(self):
        for path, field in ((self.appl, 'application_sha256'),
                            (self.root / 'tests/scenarios/scenarios.txt', 'registry_sha256')):
            original = path.read_bytes()
            path.write_bytes(original + b'\n')
            self.refuse(field)
            path.write_bytes(original)

    def test_unattestable_source(self):
        args = list(self.args)
        args[args.index('--source-tree') + 1] = '/tmp'
        self.command(['python3', str(self.helper), 'record', *args])
        self.command(['python3', str(self.helper), 'audit-matched', '--provenance', str(self.receipt), '--audit', str(self.audit)])
        result = subprocess.run(['python3', str(self.helper), 'verify', *args,
                                 '--capture', str(self.capture), '--audit', str(self.audit),
                                 '--expected-audit', str(self.expected)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source_tree_identity is unattestable', result.stderr)

    def test_symlink_capture(self):
        self.capture.unlink()
        self.capture.symlink_to(self.audit)
        self.refuse('missing finalized regular capture')

    def test_write_failure(self):
        self.receipt.unlink()
        self.receipt.mkdir()
        result = subprocess.run(['python3', str(self.helper), 'record', *self.args], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Scenario run provenance refused:', result.stderr)


if __name__ == '__main__':
    unittest.main()
