#!/usr/bin/env python3
"""Six producer-only mutations, direct native build/matches tests (no Scene).

ROOT is derived from this script. Compilation failure is never a kill.
Every exit restores original bytes and rebuilds/runs the restored candidate.
"""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[3]
CPP = 'web/src/WebAttributedLayout.cpp'
MUTATIONS = [
    ('b', 'const TextLineMetrics lineMetrics = line.metrics;',
     'const TextLineMetrics lineMetrics = line.fragmentCount ? '
     'source.metrics(source.spanStyle(result.fragment(line.firstFragment).span)) : line.metrics;',
     'first span supplies line metrics'),
    ('c', 'target.descriptor = static_cast<int>(id);',
     'target.descriptor = 0;', 'first descriptor for every fragment'),
    ('e', 'if (kind == style_internal::TEXT_REFUSED)\n          return false;',
     'if (kind == style_internal::TEXT_REFUSED)\n          continue;', 'ignore UTF-8 read refusal'),
    # Treat a segment-ending CR as a completed lone break. A following segment's
    # LF now starts its own stream: this is the separate-stream CRLF failure.
    ('f', 'previous = &segment;',
     "if (this->characters[this->characters.size() - 1].value == '\\r')\n"
     "          this->characters[this->characters.size() - 1].value = '\\n';\n"
     '        previous = &segment;', 'separate segment break streams'),
    ('i', "source.characters[i].value == '\\r' || source.characters[i].value == '\\n'",
     "source.characters[i].value == '\\n'", 'ELLIPSIS cuts only at LF'),
    ('j', 'target.byteStart = static_cast<int>(display.size());\n      target.byteEnd = static_cast<int>(end);',
     'target.byteStart = static_cast<int>(f.start);\n      target.byteEnd = static_cast<int>(f.end);',
     'source offsets on packed wire'),
]


def run(command):
    return subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)


def mutation(entry):
    letter, old, new, name = entry
    path = ROOT / CPP
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, 'mutation anchor mismatch: ' + letter
    logs = ROOT / 'build/web-attributed-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    command = ['cmake', '--build', 'build/Testing', '--target', 'LokaWebTests', '-j', '4']
    driver = ['build/Testing/web/LokaWebTests', 'attributed-only']

    def build(label):
        result = run(command)
        (logs / f'{letter}-{label}-build.log').write_text(result.stdout)
        if result.returncode:
            raise RuntimeError('Build failure is not a mutation kill:\n' + result.stdout)

    def green(label):
        result = run(driver)
        (logs / f'{letter}-{label}.log').write_text(result.stdout)
        assert result.returncode == 0, result.stdout

    build('baseline')
    green('baseline')
    try:
        path.write_text(source.replace(old, new))
        build('mutant')
        result = run(driver)
        (logs / f'{letter}-mutant.log').write_text(result.stdout)
        assert result.returncode != 0, 'surviving mutation: ' + name
        print(f'killed ({letter}): {name}', flush=True)
    finally:
        path.write_bytes(original)
        build('restored')
        green('restored')
        assert path.read_bytes() == original


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mutation', nargs='?', choices=['all'] + [m[0] for m in MUTATIONS], default='all')
    args = parser.parse_args()

    def terminated(signum, frame):
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminated)
    for entry in MUTATIONS:
        if args.mutation == 'all' or args.mutation == entry[0]:
            mutation(entry)


if __name__ == '__main__':
    main()
