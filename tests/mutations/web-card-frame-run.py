#!/usr/bin/env python3
"""Web Card frame mutations; a build failure never counts as a kill."""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CPP = 'web/src/WebCardBandContext.cpp'
MUTATIONS = [
    ('a', 'CardBodyInBand(local, props)', 'CardBodyInBand(local, CardBandProps(CARD_BAND_TOP, props.style))',
     'row.cardBody.first'),
    ('b', 'web_set_card_body(this->token(), body.x, body.y, body.width, body.height, props.style.extent());',
     '(void)body;', 'row.cardBody.first'),
    ('c', 'web_set_projected(this->token(), 1);', '', 'projected =='),
    ('d', 'web_set_projected(this->token(), 0);', '', 'projected =='),
]

def run(command, log):
    result = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(result.stdout)
    return result.returncode


def mutation(entry):
    letter, old, new, expected = entry
    path = ROOT / CPP
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, 'anchor mismatch: ' + letter
    logs = ROOT / 'build/web-card-frame-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    build = ['cmake', '--build', 'build/Testing', '--target', 'LokaWebTests', '-j', '4']
    command = ['build/Testing/web/LokaWebTests', 'card-frame-only']

    def check(label, green):
        if run(build, logs / f'{letter}-{label}-build.log'):
            raise RuntimeError(f'{letter} {label}: build failure is not a kill')
        log = logs / f'{letter}-{label}.log'
        result = run(command, log)
        if (result == 0) != green:
            raise RuntimeError(f'{letter} {label}: unexpected exit {result}')

        if not green and expected not in log.read_text():
            raise RuntimeError(f'{letter}: did not fail the expected fake-boundary assertion')

    check('baseline', True)
    try:
        path.write_text(source.replace(old, new))
        check('mutant', False)
        print(f'killed ({letter})', flush=True)
    finally:
        path.write_bytes(original)
        check('restored', True)
        assert path.read_bytes() == original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mutation', nargs='?', default='all', choices=['all'] + [e[0] for e in MUTATIONS])
    args = parser.parse_args()
    def terminate(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, terminate)
    for entry in MUTATIONS:
        if args.mutation in ('all', entry[0]):
            mutation(entry)

if __name__ == '__main__':
    main()
