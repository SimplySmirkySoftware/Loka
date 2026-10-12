#!/usr/bin/env python3
"""Run portable PowerShell behavior probes; never invoke powershell.exe in WSL."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PWSH = shutil.which('pwsh')
HELPER = ROOT / 'scripts/release/win32-remote-stage.ps1'


@unittest.skipUnless(PWSH, 'pwsh unavailable')
class PowerShellTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='win32-stage-ps-')
        self.addCleanup(self.temp.cleanup)
        self.base = pathlib.Path(self.temp.name)
        (self.base / 'drive').mkdir()
        (self.base / 'C:').symlink_to(self.base / 'drive', target_is_directory=True)
        self.work = 'C:/loka-release/' + 'a' * 12 + '-x86-xp'
        self.env = dict(os.environ, STAGE_HELPER=str(HELPER), STAGE_WORK=self.work)

    def invoke(self, mode, setup='', work=None, inline=False):
        # A PSDrive and matching relative C: directory let .NET ZipFile and the
        # PowerShell provider share Windows-shaped paths on Linux, without
        # changing the production path guard or introducing test-only doors.
        subject = ('& {\n' + HELPER.read_text() + '\n}' if inline
                   else '& $env:STAGE_HELPER')
        script = '''
$ErrorActionPreference = 'Stop'
New-PSDrive -Name C -PSProvider FileSystem -Root (Join-Path (Get-Location) 'drive') | Out-Null
''' + setup + '\n' + subject + ' -Mode ' + mode + r''' -WorkDirectory $env:STAGE_WORK -Architecture x86 -TargetProfile xp -EnvironmentCommand 'C:\rig setup\env.cmd'
exit $LASTEXITCODE
'''
        env = dict(self.env)
        if work is not None: env['STAGE_WORK'] = work
        return subprocess.run([PWSH, '-NoProfile', '-NonInteractive', '-Command', script],
                              cwd=self.base, env=env, text=True, capture_output=True)

    def good(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_prepare_unpack_list_cleanup(self):
        import hashlib
        import json
        import zipfile
        self.good(self.invoke('Prepare', inline=True))
        work = self.base / self.work
        (work / 'leftover').write_text('stale')
        self.good(self.invoke('Prepare'))
        self.assertFalse((work / 'leftover').exists())
        with zipfile.ZipFile(work / 'source.zip', 'w') as z:
            z.writestr('build/release/win32-x86-xp/App name/app.exe', b'executable')
            z.writestr('build/release/win32-x86-xp/.hidden', b'hidden')
            z.writestr('build/release/win32-x86-xp/unicode-\u65e5.txt', b'unicode')
        self.good(self.invoke('Unpack'))
        (work / 'toolchain.txt').write_bytes(b'identity\r\n')
        self.good(self.invoke('List'))
        inventory = json.loads((work / 'inventory.json').read_text())
        expected = [{'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                    for name, data in [('App name/app.exe', b'executable'), ('.hidden', b'hidden'), ('unicode-\u65e5.txt', b'unicode')]]
        self.assertEqual(sorted(inventory['files'], key=lambda r: r['path']), sorted(expected, key=lambda r: r['path']))
        with zipfile.ZipFile(work / 'stage.zip') as z:
            self.assertEqual(set(z.namelist()), {r['path'] for r in expected})
        self.good(self.invoke('Cleanup'))
        self.assertFalse(work.exists())

    def test_trap_and_path_guard(self):
        self.good(self.invoke('Prepare'))
        result = self.invoke('Unpack')  # Missing archive: terminating .NET error.
        self.assertNotEqual(result.returncode, 0)
        self.assertNotEqual(self.invoke('Cleanup', work='C:/loka-release').returncode, 0)
        self.assertTrue((self.base / self.work).is_dir())

    def test_identity_and_cmake_failure(self):
        self.good(self.invoke('Prepare'))
        self.good(self.invoke('Identity', '''
$env:VCToolsVersion = 'test-vc'
$env:WindowsSDKVersion = 'test-sdk'
function global:cmake { $global:LASTEXITCODE = 0; 'cmake version fixture'; 'second line' }
'''))
        identity = (self.base / self.work / 'toolchain.txt').read_text()
        self.assertEqual(len(identity.splitlines()), 1)
        for part in ('VCToolsVersion=test-vc', 'WindowsSDKVersion=test-sdk', 'cmake=cmake version fixture', 'OS=', 'PowerShell='):
            self.assertIn(part, identity)
        self.assertNotIn('second line', identity)
        self.assertNotEqual(self.invoke('Identity', "function global:cmake { $global:LASTEXITCODE = 7; 'broken' }").returncode, 0)

    def test_stage_runner_and_exit(self):
        self.good(self.invoke('Prepare'))
        setup = '''
New-Item -ItemType Directory -Path 'C:/rig setup' -Force | Out-Null
Set-Content -LiteralPath 'C:/rig setup/env.cmd' -Value '@exit /b 0'
function global:cmd { $global:LASTEXITCODE = 0 }
'''
        self.good(self.invoke('Stage', setup))
        runner = (self.base / self.work / 'stage.cmd').read_bytes()
        self.assertNotIn(b'\n', runner.replace(b'\r\n', b''))
        lines = runner.decode('ascii').splitlines()
        self.assertEqual(lines[1], 'call "C:\\rig setup\\env.cmd"')
        self.assertEqual(lines.count('if not "%errorlevel%"=="0" exit /b %errorlevel%'), 3)
        self.assertIn('-Mode Identity', lines[5])
        self.assertIn('-Action Release -Architecture x86 -TargetProfile xp', lines[7])
        self.assertEqual(lines[-1], 'exit /b %errorlevel%')
        self.assertNotEqual(self.invoke('Stage', setup.replace('LASTEXITCODE = 0', 'LASTEXITCODE = 29')).returncode, 0)


if __name__ == '__main__':
    if not PWSH:
        print('[skip] pwsh unavailable; PowerShell helper behavior not exercised', flush=True)
    unittest.main()
