#!/usr/bin/env python3
"""Popup mutations 1/3/4/5 are native; 2 requires Chromium and both Wasm builds.

Source bytes are restored even on SIGTERM. A compile failure is not a kill.
After an interrupted build, rebuild before trusting any binary.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[3]
CPP = 'web/src/WebPopupContext.cpp'
MUTATIONS = [
    (CPP, '  node->props.selectedIndex_.set(index, true);', '', 'selection state write', False),
    ('web/js/library.js', 'if (row.selected !== undefined) element.selectedIndex = row.selected;',
     '', 'restore before export', True),
    (CPP, '    web_set_selected(this->token(), 1, count == 0 ? -1 : selected < 0 ? 0 : selected >= count ? count - 1 : selected);',
     '    (void)selected;', 'render selected publication', False),
    (CPP, '  if (index < 0 || index >= this->projectedCount())\n    return false;',
     '', 'full integer range', False),
    (CPP, '    web_set_projected(this->token(), 0);', '', 'layout refusal visibility', False),
]


def run(command):
    return subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('number', type=int, choices=range(1, 6))
    args = parser.parse_args()
    relative, old, new, name, browser = MUTATIONS[args.number - 1]
    if browser and not (os.environ.get('CHROMIUM') or any(shutil.which(c) for c in
                         ['chromium', 'chromium-browser', 'google-chrome'])):
        raise SystemExit('Mutation 2 requires Chromium: a skip is not a verdict')
    path = ROOT / relative
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == (3 if args.number == 5 else 1), 'mutation anchor mismatch'
    logs = ROOT / 'build/web-popup-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    builds = ([['cmake', '--build', '--preset', preset, '-j', '4'] for preset in ['web', 'testing-web']]
              if browser else [['cmake', '--build', 'build/Testing', '--target', 'LokaWebTests', '-j', '4']])
    driver = (['node', 'tests/web/driver/run.mjs', 'build/wasm/testing/web'] if browser
              else ['build/Testing/web/LokaWebTests', 'popup-only'])

    def build():
        for command in builds:
            result = run(command)
            if result.returncode:
                raise RuntimeError('Build failure is not a mutation kill:\n' + result.stdout)

    def green(label):
        result = run(driver)
        (logs / f'{args.number}-{label}.log').write_text(result.stdout)
        assert result.returncode == 0 and '[skip] browser driver' not in result.stdout, result.stdout

    build()
    green('baseline')

    def terminated(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminated)
    try:
        path.write_text(source.replace(old, new))
        build()
        result = run(driver)
        (logs / f'{args.number}-mutant.log').write_text(result.stdout)
        assert result.returncode != 0, 'surviving mutation: ' + name
        print('killed:', name)
    finally:
        path.write_bytes(original)
        build()
        green('restored')
        assert path.read_bytes() == original


if __name__ == '__main__':
    main()
