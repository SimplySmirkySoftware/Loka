#!/usr/bin/env python3
"""Exercise the Bash transport using local SSH/SCP stand-ins, never a rig."""
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
STUB = r'''#!/usr/bin/env python3
import base64, hashlib, json, os, pathlib, re, shutil, sys, zipfile
root = pathlib.Path(os.environ['REMOTE_FIXTURE'])
fault = os.environ.get('REMOTE_FAULT', '')
args = sys.argv[1:]
assert args[:2] == ['-o', 'BatchMode=yes'], args
args = args[2:]
work = root / 'work'
if pathlib.Path(sys.argv[0]).name == 'ssh':
    assert args[0] == 'fixture-rig'
    command = base64.b64decode(args[1].split()[-1]).decode('utf-16le')
    mode = re.search(r" -Mode '([^']+)'", command)[1]
    remote = re.search(r" -WorkDirectory '([^']+)'", command)[1]
    assert re.fullmatch(r'C:/loka-release/[0-9a-f]{12}-x(64|86)-(modern|xp)', remote)
    with (root / 'calls').open('a') as log: log.write(mode + '\n')
    if mode == 'Prepare':
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir()
    elif mode == 'Unpack':
        with zipfile.ZipFile(work / 'source.zip') as archive:
            assert archive.read('tracked.txt') == b'committed source\n'
            assert not any(n.startswith('build/') for n in archive.namelist())
            archive.extractall(work / 'source')
    elif mode == 'Stage':
        stage = work / 'stage'
        (stage / 'App with space').mkdir(parents=True)
        (stage / 'App with space' / 'app.exe').write_bytes(b'new executable')
        (stage / 'README.txt').write_bytes(b'release readme')
        (stage / '.hidden').write_bytes(b'hidden')
        (stage / 'unicode-\u65e5.txt').write_bytes(b'utf8 path')
        (work / 'toolchain.txt').write_bytes(b'VCToolsVersion=fixture; WindowsSDKVersion=fixture; cmake=fixture; OS=fixture; PowerShell=fixture\r\n')
        if fault == 'stage': sys.exit(23)
    elif mode == 'List':
        def record(p, name):
            return dict(path=name, size=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
        stage = work / 'stage'
        records = [record(p, p.relative_to(stage).as_posix()) for p in stage.rglob('*') if p.is_file()]
        if fault == 'size': records[0]['size'] += 1
        if fault == 'hash': records[0]['sha256'] = '0' * 64
        if fault == 'missing': records.append(dict(path='missing', size=0, sha256='0'*64))
        if fault == 'extra': records.pop()
        manifest = dict(files=records, toolchain=record(work / 'toolchain.txt', 'toolchain.txt'))
        (work / 'inventory.json').write_text(json.dumps(manifest))
        with zipfile.ZipFile(work / 'stage.zip', 'w') as archive:
            for p in stage.rglob('*'):
                if p.is_file(): archive.write(p, p.relative_to(stage).as_posix())
            if fault == 'unsafe': archive.writestr('../escape', b'bad')
        if fault == 'identity': (work / 'toolchain.txt').write_bytes(b'corrupt')
    elif mode == 'Cleanup':
        shutil.rmtree(work)
    else: raise AssertionError(mode)
else:
    source, target = args
    if source.startswith('fixture-rig:'):
        if fault == 'transfer': sys.exit(24)
        shutil.copyfile(work / source.rsplit('/', 1)[1], target)
    else:
        assert target.startswith('fixture-rig:C:/loka-release/')
        shutil.copyfile(source, work / target.rsplit('/', 1)[1])
'''


class RemoteStageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='win32-remote-stage-')
        self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name)
        self.repo = self.base / 'repo'
        self.repo.mkdir()
        scripts = self.repo / 'scripts/release'
        scripts.mkdir(parents=True)
        for p in (ROOT / 'scripts/release').glob('win32-remote-stage*'):
            shutil.copy2(p, scripts / p.name)
        (self.repo / '.gitignore').write_text('/build/\n')
        (self.repo / 'tracked.txt').write_text('committed source\n')
        for args in [('init', '-q'), ('add', '.'), ('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture')]:
            subprocess.run(['git', *args], cwd=self.repo, check=True, capture_output=True)
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        for name in ('ssh', 'scp'):
            p = self.bin / name
            p.write_text(STUB)
            p.chmod(0o755)
        self.remote = self.base / 'remote'
        self.remote.mkdir()
        self.destination = self.repo / 'build/release/win32-x64'
        self.destination.mkdir(parents=True)
        (self.destination / 'old.txt').write_bytes(b'previous')
        self.identity = self.destination.with_name('win32-x64.toolchain.txt')
        self.identity.write_bytes(b'previous toolchain')

    def invoke(self, fault='', arguments=None, cwd=None):
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                   REMOTE_FIXTURE=str(self.remote), REMOTE_FAULT=fault)
        return subprocess.run(['bash', str(self.repo / 'scripts/release/win32-remote-stage.sh'),
                               *(arguments if arguments is not None else ['fixture-rig', 'x64', 'modern', r'C:\rig setup\release-env.cmd'])],
                              cwd=cwd or self.repo, env=env, capture_output=True, text=True)

    def assert_previous(self):
        self.assertEqual({p.name for p in self.destination.iterdir()}, {'old.txt'})
        self.assertEqual((self.destination / 'old.txt').read_bytes(), b'previous')
        self.assertEqual(self.identity.read_bytes(), b'previous toolchain')

    def test_success(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.destination / 'App with space/app.exe').read_bytes(), b'new executable')
        self.assertFalse((self.destination / 'old.txt').exists())
        self.assertIn(b'PowerShell=fixture', self.identity.read_bytes())
        self.assertFalse((self.remote / 'work').exists())
        self.assertEqual((self.remote / 'calls').read_text().splitlines(), ['Prepare', 'Unpack', 'Stage', 'List', 'Cleanup'])
        self.assertFalse(list(self.destination.parent.glob('.remote-stage.*')))

    def test_xp_success(self):
        result = self.invoke(arguments=['fixture-rig', 'x86', 'xp', r'C:\rig\xp.cmd'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.destination.parent / 'win32-x86-xp/README.txt').is_file())
        self.assertTrue((self.destination.parent / 'win32-x86-xp.toolchain.txt').is_file())
        self.assert_previous()

    def test_dirty(self):
        for kind in ('tracked', 'untracked', 'staged'):
            with self.subTest(kind=kind):
                p = self.repo / ('tracked.txt' if kind == 'tracked' else 'new.txt')
                p.write_text('dirty')
                if kind == 'staged': subprocess.run(['git', 'add', '.'], cwd=self.repo, check=True)
                result = self.invoke()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('clean', result.stderr)
                self.assert_previous()
                subprocess.run(['git', 'reset', '--hard', '-q'], cwd=self.repo, check=True)
                if p.name == 'new.txt' and p.exists(): p.unlink()
        self.assertFalse((self.remote / 'calls').exists())

    def test_bad_arguments(self):
        for args in ([], ['fixture-rig', 'arm64', 'modern', r'C:\rig\env.cmd'],
                     ['fixture-rig', 'x64', 'wrong', r'C:\rig\env.cmd'],
                     ['-option', 'x64', 'modern', r'C:\rig\env.cmd'],
                     ['fixture-rig', 'x64', 'modern', 'relative.cmd'],
                     ['fixture-rig', 'x64', 'modern', 'C:\\rig\\evil%PATH%.cmd']):
            with self.subTest(args=args):
                self.assertNotEqual(self.invoke(arguments=args).returncode, 0)
                self.assert_previous()
        self.assertFalse((self.remote / 'calls').exists())

    def test_xp_x64_refused_early(self):
        result = self.invoke(arguments=['fixture-rig', 'x64', 'xp', r'C:\rig\env.cmd'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('xp requires x86', result.stderr)
        self.assertFalse((self.remote / 'calls').exists())
        self.assert_previous()

    def test_checkout_root(self):
        for cwd in (self.repo / 'scripts', self.base):
            result = self.invoke(cwd=cwd)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('top of a Git checkout', result.stderr)
            self.assert_previous()

    def test_remote_failure(self):
        result = self.invoke('stage')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('remote work retained at fixture-rig:C:/loka-release/', result.stderr)
        self.assertEqual((self.remote / 'calls').read_text().splitlines(), ['Prepare', 'Unpack', 'Stage'])
        self.assert_previous()

    def test_verification_and_transfer_failures(self):
        for fault in ('hash', 'size', 'missing', 'extra', 'identity', 'transfer', 'unsafe'):
            with self.subTest(fault=fault):
                result = self.invoke(fault)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn('remote work retained', result.stderr)
                self.assertTrue((self.remote / 'work').is_dir())
                self.assert_previous()

    def test_publish_rollback(self):
        spec = importlib.util.spec_from_file_location('verifier', ROOT / 'scripts/release/win32-remote-stage-verify.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        scratch = self.destination.parent / 'scratch'
        scratch.mkdir()
        incoming = scratch / 'incoming'
        incoming.mkdir()
        (incoming / 'new.txt').write_text('new')
        identity = scratch / 'toolchain.txt'
        identity.write_text('new identity')
        original = pathlib.Path.rename
        def fail_identity(path, target):
            if path == identity:
                raise OSError('injected second publish rename failure')
            return original(path, target)
        with mock.patch.object(pathlib.Path, 'rename', fail_identity):
            with self.assertRaises(OSError):
                module.publish(scratch, incoming, self.destination)
        self.assert_previous()


if __name__ == '__main__':
    unittest.main()
