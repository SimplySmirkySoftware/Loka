#!/usr/bin/env python3
"""PR 16 workspace-install mutants. Source emsdk and set LOKA_VSCODE first.

A full passing smoke is required before mutation, and again after restoration.
SIGINT/SIGTERM unwind through source restoration. SIGKILL cannot be recovered by
any in-process handler; the original bytes are also saved under build/.
"""
import argparse
import os
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MUTATIONS = [
    ('tools/vscode-loka/scripts/place.cjs',
     '    manifest.main = `./${generation}/extension.js`;',
     '    // Mutation: leave main pointing to the source build layout.',
     'W3', 'manifest main not rewritten'),
    ('tools/vscode-loka/test/smoke/workspace.mjs',
     '    place(workspace, payload);',
     '    // Mutation: omit second placement.',
     'W4', 'second placement omitted'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('number', type=int, choices=(1, 2))
    args = parser.parse_args()
    if not os.environ.get('LOKA_VSCODE'):
        raise SystemExit('LOKA_VSCODE required: a skip is not a mutation verdict')
    relative, old, new, cell, name = MUTATIONS[args.number - 1]
    path = ROOT / relative
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, 'mutation anchor must be unique'
    logs = ROOT / 'build/web-workspace-install-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    (logs / (str(args.number) + '.original')).write_bytes(original)
    environment = {**os.environ, 'LOKA_REQUIRE_VSCODE_SMOKE': '1'}

    def run(label, command):
        log = logs / (str(args.number) + '-' + label + '.log')
        with log.open('w') as output:
            result = subprocess.run(command, cwd=ROOT, env=environment,
                                    stdout=output, stderr=subprocess.STDOUT)
        return result.returncode, log.read_text()

    def passing(label):
        code, output = run(label, ['bash', 'tests/scripts/VscodeLokaSmokeTest.sh'])
        assert code == 0 and '[pass] W\n' in output and '[pass] WR\n' in output, output

    code, output = run('build', ['npm', 'run', 'build', '--prefix', 'tools/vscode-loka'])
    assert code == 0, output
    passing('baseline')

    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, interrupted)
    try:
        path.write_text(source.replace(old, new, 1))
        code, output = run('mutant', ['bash', 'tests/scripts/VscodeLokaSmokeTest.sh'])
        assert code != 0 and '[fail] vscode smoke: ' + cell + ':' in output, \
            'Not a discriminating ' + cell + ' failure:\n' + output
        if cell == 'W4':
            assert '[pass] W3:' in output and 'workspace status LokaW16:' in output, output
        print('killed:', name)
    finally:
        # Restore before any more fallible operation, including the restored smoke.
        path.write_bytes(original)
        assert path.read_bytes() == original
    passing('restored')


if __name__ == '__main__':
    main()
