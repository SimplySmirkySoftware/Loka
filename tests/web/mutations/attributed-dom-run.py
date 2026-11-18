#!/usr/bin/env python3
"""Consumer mutations. Build failure is never a kill; restore bytes on every exit.
All six mutations are native/JS-unit killable; Chromium also pins child identity.
"""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MUTATIONS = [
    ('m', 'web/src/WebAttributedTextContext.cpp', 'if (!value.valid())',
     'if (false && !value.valid())', 'keep the old snapshot for an invalid source', 'native'),
    ('a', 'web/src/WebScenePlatformController.cpp',
     '  if (node->asAttributedTextNode())\n    return AttributedTextHandler.ensureContext(node, this, state) != 0;',
     '', 'remove projected preparation', 'native'),
    ('d', 'web/js/library.js', 'if(previous && previous.words.length===wordCount',
     'if(false && previous && previous.words.length===wordCount', 'remove equality shortcut', 'js'),
    ('g', 'web/js/library.js', 'const status=LokaWebStyle.webRunsStatus, row=WebDOM.row(token);',
     'const status=LokaWebStyle.webRunsStatus, row=WebDOM.row(token);\n'
     '    if(row) row.element.replaceChildren(document.createDocumentFragment());',
     'clear children before validation', 'js'),
    ('h', 'web/src/WebAttributedTextContext.cpp', '    this->controller_.requestRelayout();',
     '    (void)status;', 'omit apply retry', 'native'),
    ('k', 'web/src/WebAttributedTextContext.cpp', 'if (bottom > SHRT_MAX)',
     'if (false && bottom > SHRT_MAX)', 'drop absolute bottom check', 'native'),
]


def run(command):
    return subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def mutation(entry):
    letter, filename, old, new, name, kind = entry
    path = ROOT / filename
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, 'anchor mismatch: ' + letter
    logs = ROOT / 'build/web-attributed-dom-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    build = ['cmake', '--build', 'build/Testing', '--target', 'LokaWebTests', '-j', '4']
    driver = (['build/Testing/web/LokaWebTests', 'attributed-dom-only'] if kind == 'native'
              else ['node', '--test', 'tests/web/js/attributed.test.mjs'])

    def execute(label, expect_green):
        if kind == 'native':
            result = run(build)
            (logs / f'{letter}-{label}-build.log').write_text(result.stdout)
            if result.returncode:
                raise RuntimeError('Build failure is not a kill:\n' + result.stdout)
        result = run(driver)
        (logs / f'{letter}-{label}.log').write_text(result.stdout)
        assert (result.returncode == 0) == expect_green, label + ': ' + result.stdout

    execute('baseline', True)
    try:
        path.write_text(source.replace(old, new))
        execute('mutant', False)
        print(f'killed ({letter}): {name}', flush=True)
    finally:
        path.write_bytes(original)
        execute('restored', True)
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
