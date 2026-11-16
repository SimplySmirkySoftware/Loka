#!/usr/bin/env python3
"""Build the real Web examples and compare each preset's catalog to artifacts.

This integration test removes and rebuilds example JS/Wasm outputs in both
preset build directories; stop browser drivers, previews, and other builds
using those directories before running it.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    if not all(shutil.which(tool) for tool in ('emcmake', 'emcc', 'node')):
        print('[skip] Web catalog integration requires emsdk (emcmake, emcc and node on PATH)')
        return 77
    expected = [('HelloWorld', 'LokaHelloWeb'), ('MineSweeper', 'LokaMineWeb'),
                ('Tutorial', 'LokaTutorialWeb')]
    for preset, directory in (('web', 'web'), ('testing-web', 'testing')):
        subprocess.run(['emcmake', 'cmake', '--preset', preset], cwd=ROOT, check=True)
        # Remove the expected pairs first: a stale pair left by an earlier build
        # must not stand in for output that this build wrote somewhere else.
        for name, target in expected:
            for suffix in ('.js', '.wasm'):
                stale = ROOT / 'build/wasm' / directory / 'example' / name / (target + suffix)
                if stale.exists():
                    stale.unlink()
        subprocess.run(['cmake', '--build', '--preset', preset, '-j8', '--target',
                        *[target for _, target in expected]], cwd=ROOT, check=True)
        binary = ROOT / 'build/wasm' / directory
        text = (binary / 'loka-web-apps.json').read_text()
        print(f'{preset}: {text}', end='', flush=True)
        catalog = json.loads(text)
        wanted = {'version': 1, 'apps': [
            {'id': target, 'name': name, 'target': target, 'exportName': target,
             'sourceDir': f'example/{name}', 'js': f'example/{name}/{target}.js'}
            for name, target in expected]}
        if catalog != wanted:
            raise AssertionError(f'{preset}: unexpected catalog: {catalog!r}')
        for row in catalog['apps']:
            js = binary / row['js']
            wasm = js.with_suffix('.wasm')
            if not js.is_file() or not wasm.is_file():
                raise AssertionError(f'{preset}: missing JS/Wasm pair for {row["id"]}')
            if row['exportName'] not in js.read_text():
                raise AssertionError(f'{preset}: factory missing from JS')
            if wasm.read_bytes()[:4] != b'\0asm':
                raise AssertionError(f'{preset}: invalid Wasm artifact')
        print(f'[pass] {preset}: exact catalog and all built JS/Wasm pairs', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
