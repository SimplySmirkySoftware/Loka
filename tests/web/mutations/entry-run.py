#!/usr/bin/env python3
"""Build behavioral entry mutants, require a green browser baseline, restore bytes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
number = int(sys.argv[2])
logdir = root / 'build/web-entry-mutations'
logdir.mkdir(parents=True, exist_ok=True)


def run(args, label):
    result = subprocess.run(args, cwd=root, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    (logdir / ('%d-%s.log' % (number, label))).write_text(result.stdout)
    return result


def dirty():
    return len(subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=all'], cwd=root).splitlines())


path = root / ('web/entry/WebEntry.cpp' if number == 1 else 'tests/web/BrowserMain.cpp')
original = path.read_bytes()
source = original.decode()
if number == 1:
    replacements = [('return session->start(&loka::web::CreateAppConfig, emscripten_get_now());',
                     'return WebSession::FAILED;')]
elif number == 2:
    replacements = [
        ('      vehicle.roots = webtest::staged.roots;\n      vehicle.destroy = webtest::staged.destroy;\n', ''),
        ('webtest::staged.roots = roots;', 'vehicle.roots = roots;'),
        ('webtest::staged.destroy = destroy != 0;', 'vehicle.destroy = destroy != 0;')]
else:
    raise ValueError(number)
for old, new in replacements:
    if source.count(old) != 1:
        raise RuntimeError('mutation anchor missing or ambiguous: ' + old)
    source = source.replace(old, new, 1)

builds = [
    ['cmake', '--build', '--preset', 'web', '-j8', '--target', 'LokaHelloWeb', 'LokaWebCounter'],
    ['cmake', '--build', '--preset', 'testing-web', '-j8', '--target', 'LokaWebCounter']]
command = [shutil.which('node') or os.environ['EMSDK_NODE'],
           'tests/web/driver/run.mjs', 'build/wasm/testing/web']
before = dirty()
killed = False
changed = False
try:
    for index, build in enumerate(builds):
        result = run(build, 'baseline-build-%d' % index)
        if result.returncode:
            raise RuntimeError('baseline build failed: ' + result.stdout)
    baseline = run(command, 'baseline')
    if baseline.returncode or '[skip]' in baseline.stdout:
        raise RuntimeError('browser baseline unavailable; no mutation kill: ' + baseline.stdout)
    path.write_text(source)
    changed = True
    for index, build in enumerate(builds):
        result = run(build, 'mutant-build-%d' % index)
        if result.returncode:
            raise RuntimeError('compile failure is not a mutation kill: ' + result.stdout)
    result = run(command, 'mutant')
    # Require the intended assertion, not any incidental driver/environment failure.
    discriminator = 'HelloWeb start' if number == 1 else 'configure after start is inert'
    killed = result.returncode != 0 and discriminator in result.stdout
finally:
    path.write_bytes(original)
    if changed:
        for index, build in enumerate(builds):
            result = run(build, 'restored-build-%d' % index)
            if result.returncode:
                raise RuntimeError('restoration build failed: ' + result.stdout)
        restored = run(command, 'restored')
        if restored.returncode:
            raise RuntimeError('restored baseline failed: ' + restored.stdout)
    after = dirty()
    print('mutation %d: %s; dirty before=%d after=%d' %
          (number, 'killed' if killed else 'survived', before, after), flush=True)
    if before != after or path.read_bytes() != original:
        raise RuntimeError('restoration changed source bytes or dirty count')
if not killed:
    sys.exit(1)
