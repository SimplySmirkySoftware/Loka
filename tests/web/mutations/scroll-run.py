#!/usr/bin/env python3
"""Behavioral scroll mutants; exact restoration, compile success is never a kill."""
import os
import re
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
number = int(sys.argv[2])
logdir = root / 'build/web-scroll-mutations'
logdir.mkdir(parents=True, exist_ok=True)
mutants = {
  1: ('web/src/WebWindow.cpp', '    this->requestSceneRelayout();', '    {} // mutation: reveal does not schedule', 'row.originY == body.viewport.get().y + 7'),
  2: ('web/src/WebScrollContext.cpp', 'node->publishViewport(x, y, candidate->width(), candidate->height());', 'node->publishViewport(target.x, target.y, candidate->width(), candidate->height());', 'body.viewport.get().x == 11'),
  3: ('web/js/library.js', 'HEAP32[appliedX>>2]=ax; HEAP32[appliedY>>2]=ay;', 'HEAP32[appliedX>>2]=originX; HEAP32[appliedY>>2]=originY;', '12 !== 10'),
  4: ('web/js/library.js', "row.listener = () => Module['_web_scroll'](token, Math.round(element.scrollLeft), Math.round(element.scrollTop));", "const cachedX = Math.round(element.scrollLeft), cachedY = Math.round(element.scrollTop); row.listener = () => Module['_web_scroll'](token, cachedX, cachedY);", 'listener samples current offsets'),
  5: ('web/src/WebScrollContext.cpp', 'node->publishViewport(x, y, candidate->width(), candidate->height());', 'node->publishViewport(x, y - node->props.contentInsets_.top(), candidate->width(), candidate->height());', 'body.viewport.get() == loka::core::Frame(0, 23, 185, 88)'),
  6: ('web/src/WebScenePlatformController.cpp', '  if (this->projectionParents_.current().hasShortRangeRefusal()) return received.y;', '  // mutation: dispatch continues after channel refusal', 'texts == 1'),
  7: ('web/src/WebScrollContext.cpp', '  if (!this->metrics_) return false;\n  int x = 0, y = 0;', '  if (!this->metrics_) return false;\n  return true; // mutation: request application no-op\n  int x = 0, y = 0;', 'body.viewport.get().x == 11')}
relative, old, new, discriminator = mutants[number]
# Consumer mutant must cover request read-back as well as the layout consumer.
path = root / relative
original = path.read_bytes()
source = original.decode()
pattern = r'\s+'.join(re.escape(part) for part in old.split())
matches = list(re.finditer(pattern, source))
if len(matches) != 1:
    raise RuntimeError('mutation anchor missing or ambiguous: ' + old)
match = matches[0]
source = source[:match.start()] + new + source[match.end():]
if number == 2:
    source = source.replace('this->owner()->asScrollViewNode()->publishOrigin(x, y);', 'this->owner()->asScrollViewNode()->publishOrigin(target.x, target.y);', 1)
env = dict(os.environ, LOKA_WEB_CAPTURE_SCROLL='1', LOKA_WEB_MUTATION_REVEAL='1')
command = ['node', 'tests/web/js/boundary.test.mjs'] if number in (3, 4) else ['build/Testing/web/LokaWebTests', 'scroll-only']
build = ['cmake', '--build', '--preset', 'testing', '-j8', '--target', 'LokaWebTests']
def run(args, label):
    result = subprocess.run(args, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (logdir / ('%d-%s.log' % (number, label))).write_text(result.stdout)
    return result

def dirty():
    return len(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all'], cwd=root).splitlines())

before = dirty()
killed = False
try:
    if number not in (3, 4) and run(build, 'baseline-build').returncode:
        raise RuntimeError('baseline build failed')
    if run(command, 'baseline').returncode:
        raise RuntimeError('baseline failed')
    path.write_text(source)
    if number not in (3, 4) and run(build, 'mutant-build').returncode:
        raise RuntimeError('mutant did not compile; not a kill')
    result = run(command, 'mutant')
    killed = result.returncode != 0 and discriminator in result.stdout
finally:
    path.write_bytes(original)
    if number not in (3, 4) and run(build, 'restored-build').returncode:
        raise RuntimeError('restored build failed')
    restored = run(command, 'restored')
    after = dirty()
    print('mutation %d: %s; dirty before=%d after=%d' % (number, 'killed' if killed else 'survived', before, after), flush=True)
    if path.read_bytes() != original or before != after or restored.returncode:
        raise RuntimeError('restoration failed')
if not killed:
    sys.exit(1)
