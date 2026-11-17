#!/usr/bin/env python3
"""RectSurface mutations: all three run natively through mounted Scenes.

Chromium is separately required for CSS bounds/decoration/listener evidence.
Source bytes are restored even on SIGTERM. A compile failure is not a kill.
"""
import argparse
from pathlib import Path
import signal
import subprocess

ROOT = Path(__file__).resolve().parents[3]
CPP = 'web/src/WebRectSurfaceContext.cpp'
MUTATIONS = [
    ('web/src/WebScenePlatformController.cpp',
     """  if (node->asRectSurfaceNode())
  {
    WebRectSurfaceContext *context =
        static_cast<WebRectSurfaceContext *>(RectSurfaceHandler.ensureContext(node, this, state));
    return context ? context->place(state) : state.y;
  }
""", '', 'dedicated layoutNode branch'),
    (CPP, 'const int height = props.height_ > 0 ? props.height_ : state.height;',
     'const int height = state.height;', 'explicit height'),
    (CPP, '    web_set_projected(this->token(), 0);', '', 'refusal hides projection'),
]


def run(command):
    return subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('number', type=int, choices=range(1, 4))
    args = parser.parse_args()
    relative, old, new, name = MUTATIONS[args.number - 1]
    path = ROOT / relative
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == (2 if args.number == 3 else 1), 'mutation anchor mismatch'
    logs = ROOT / 'build/web-rect-surface-mutations'
    logs.mkdir(parents=True, exist_ok=True)
    builds = [['cmake', '--build', 'build/Testing', '--target', 'LokaWebTests', '-j', '4']]
    driver = ['build/Testing/web/LokaWebTests', 'rect-surface-only']

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
