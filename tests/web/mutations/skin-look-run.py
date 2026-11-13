#!/usr/bin/env python3
"""Page 14 mutation checks; run with emsdk sourced and Chromium available.

Each mutant is compiled before its discriminator runs. Exact source bytes are
restored in finally; the restored build and discriminator must pass as well.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
JS = 'web/js/library.js'
CONTROLLER = 'web/src/WebScenePlatformController.cpp'
MUTATIONS = [
    (JS, 'kind===0 ? look.root', 'kind===0 ? {}', 'root paint'),
    (JS, 'kind===2 ? look.button', 'kind===2 ? {}', 'Button paint'),
    (CONTROLLER, 'Handler<CellNode, WebElementContext, loka::web::CELL>',
     'Handler<CellNode, WebElementContext, loka::web::BUTTON>', 'Cell kind'),
    (CONTROLLER, 'state = b->props.text_;\n        block = BlockStyle().align(TEXT_ALIGN_CENTER);',
     'state = b->props.text_;', 'Button alignment'),
    (JS, "else row.element.style.setProperty('--loka-text',LokaWebStyle.skinLook[WebDOM.skin].disabledText);",
     'else void 0;', 'disabled variable'),
    (JS, "if(value) row.element.style.removeProperty('--loka-text');", 'if(value) void 0;', 'enabled variable removal'),
    (JS, "web_skin: () => WebDOM.skin = LokaWebStyle.resolveSkin(Module['lokaSkin']),",
     "web_skin: () => LokaWebStyle.resolveSkin(Module['lokaSkin']),", 'stored skin'),
    ('web/src/WebResources.cpp', ' || this->kind == loka::web::CELL', '', 'Cell leaf'),
    ('web/src/WebSession.cpp', '(row->kind != loka::web::BUTTON && row->kind != loka::web::CELL)',
     'row->kind != loka::web::BUTTON', 'Cell click admission'),
]

def run(command):
    return subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('number', type=int, choices=range(1, 10))
    args = parser.parse_args()
    if not (os.environ.get('CHROMIUM') or any(shutil.which(c) for c in ['chromium','chromium-browser','google-chrome'])):
        raise SystemExit('Chromium required: a skip is not a mutation verdict')
    relative, old, new, name = MUTATIONS[args.number-1]
    path = ROOT / relative
    original = path.read_bytes()
    source = original.decode()
    assert source.count(old) == 1, 'mutation anchor must be unique'
    log = ROOT / 'build/web-skin-look-mutations'
    log.mkdir(parents=True, exist_ok=True)
    builds = [['cmake','--build','--preset','web','-j','4'],
              ['cmake','--build','--preset','testing-web','-j','4']]
    driver = ['node','tests/web/driver/run.mjs','build/wasm/testing/web']
    def build():
        for command in builds:
            result = run(command)
            if result.returncode:
                raise RuntimeError('build failure is not a mutation kill:\n'+result.stdout)
    build()
    baseline = run(driver)
    assert baseline.returncode == 0 and '[skip] browser driver' not in baseline.stdout, baseline.stdout
    try:
        path.write_text(source.replace(old,new,1))
        build()
        result = run(driver)
        (log / (str(args.number)+'.log')).write_text(result.stdout)
        assert result.returncode != 0, 'surviving mutation: '+name
        print('killed:',name)
    finally:
        path.write_bytes(original)
        build()
        restored = run(driver)
        assert restored.returncode == 0, restored.stdout
        assert path.read_bytes() == original

if __name__ == '__main__':
    main()
