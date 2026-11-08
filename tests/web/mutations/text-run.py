#!/usr/bin/env python3
"""Reversible text-wire mutations; preserve exact bytes and report dirty counts."""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
number = int(sys.argv[2])
node = shutil.which('node') or os.environ.get('EMSDK_NODE')
if number in (2, 3, 4, 5) and not node:
    raise SystemExit('node unavailable: source emsdk_env.sh or set EMSDK_NODE')

def run(args):
    return subprocess.run(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

def dirty():
    return len(run(['git', 'status', '--porcelain', '--untracked-files=all']).stdout.splitlines())

path = root / ('web/js/style-vocab.js' if number in (1, 2) else
               'tools/style-vocab.json' if number == 6 else 'web/js/text.js')
original = path.read_bytes()
before = dirty()
command = ([sys.executable, 'tools/ci/check_style_vocab.py'] if number == 1 else
           [node, 'tests/web/js/boundary.test.mjs'] if number == 2 else
           [sys.executable, 'tests/web/js/schema.test.py'] if number == 6 else
           [node, 'tests/web/js/decode.test.mjs'])
logdir = root / 'build/web-text-mutations'
logdir.mkdir(parents=True, exist_ok=True)
killed = False
try:
    baseline = run(command)
    if baseline.returncode:
        raise RuntimeError('baseline failed: ' + baseline.stdout)
    source = original.decode()
    if number == 1:
        mutant = source + '// corrupt generated output\n'
    elif number == 2:
        mutant = re.sub(r'"fingerprint":\d+', '"fingerprint":0', source)
    elif number == 3:
        mutant = source.replace('el.style.setProperty(row[1], values[i]);',
                                "if (row[1] !== 'letter-spacing') el.style.setProperty(row[1], values[i]);")
    elif number == 4:
        mutant = source.replace('Math.abs(points - this.sizes[i]) < Math.abs(points - this.sizes[index])',
                                'Math.abs(points - this.sizes[i]) <= Math.abs(points - this.sizes[index])')
    elif number == 5:
        mutant = source.replace('if (d.sizeKind === tags.ROLE && !w.ranges.sizePayload.ROLE.includes(d.sizePayload)) return null;', '')
    elif number == 6:
        value = json.loads(source)
        fields = value['wire']['textDescriptor']['fields']
        fields[0], fields[1] = fields[1], fields[0]
        mutant = json.dumps(value, indent=2)+'\n'
    else:
        raise ValueError(number)
    if mutant == source:
        raise RuntimeError('mutation anchor missing')
    path.write_text(mutant)
    if number == 2:
        print('mutation 2: JS import pin; linked start/completed coverage belongs to the browser driver')
    if number in (2, 3, 4, 5):
        syntax = run([node, '--check', str(path)])
        if syntax.returncode:
            raise RuntimeError('invalid mutant is not a kill: '+syntax.stdout)
    if number == 6:
        spec = importlib.util.spec_from_file_location('generator', root / 'tools/ci/gen_style_vocab.py')
        g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
        a = g.resolved_schema(json.loads(source)); b = g.resolved_schema(json.loads(mutant))
        killed = a['count'] == b['count'] and g.fingerprint(a) != g.fingerprint(b)
        result = 'same-count reorder fingerprint: %08x -> %08x\n' % (g.fingerprint(a), g.fingerprint(b))
    else:
        outcome = run(command)
        killed = outcome.returncode != 0
        result = outcome.stdout
    (logdir / ('%d.log' % number)).write_text(result)
finally:
    path.write_bytes(original)
    restored = run(command)
    after = dirty()
    print('mutation %d: %s; dirty before=%d after=%d' % (number, 'killed' if killed else 'survived', before, after))
    if restored.returncode or before != after:
        raise RuntimeError('restoration failed: '+restored.stdout)
if not killed:
    sys.exit(1)
