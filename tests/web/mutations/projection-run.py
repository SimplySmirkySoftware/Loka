#!/usr/bin/env python3
"""Compile each mutant, run its discriminator, restore exact bytes and rebuild."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
root = Path(sys.argv[1]).resolve()
number = int(sys.argv[2])

def run(args):
    return subprocess.run(args, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

def dirty():
    return len(run(['git','status','--porcelain','--untracked-files=all']).stdout.splitlines())

relative = 'web/src/WebScenePlatformController.cpp' if number in (1,3,6) else 'web/src/WebTextLayout.hpp' if number == 2 else 'web/src/WebTextLayout.cpp'
path = root / relative
original = path.read_bytes()
source = original.decode()
if number == 1:
    old = 'web_set_text(this->element_.token(), snapshot->joined(), snapshot->joinedLength());'
    new = 'std::string propsBytes; loka::platform::CollectUtf8(input.text, propsBytes); web_set_text(this->element_.token(), propsBytes.data(), static_cast<unsigned>(propsBytes.size()));'
elif number == 2:
    old = '&& this->availableWidth == other.availableWidth'
    new = ''
elif number == 3:
    old = 'if (status == WEB_MEASURE_REFUSED)'
    new = 'if (status == WEB_MEASURE_REFUSED || status == WEB_MEASURE_PROTOCOL)'
elif number == 4:
    old = 'if (this->rows_[index].occupied)'
    new = 'if (false && this->rows_[index].occupied)'
elif number == 5:
    old = 'int measured = 0;'
    new = '''int measured = 0;
    if (start) {
      int prefix = 0, full = 0;
      this->status_ = CheckedStatus(web_measure(this->descriptor_.data(), this->snapshot_.prepared_, static_cast<unsigned>(end), &full));
      if (!this->valid()) return false;
      this->status_ = CheckedStatus(web_measure(this->descriptor_.data(), this->snapshot_.prepared_, static_cast<unsigned>(start), &prefix));
      if (!this->valid()) return false;
      out = full - prefix; return true;
    }'''
elif number == 6:
    old = 'web_set_projected(this->element_.token(), 0);'
    new = ''
else:
    raise ValueError(number)
if old not in source:
    raise RuntimeError('mutation anchor missing')
before = dirty()
killed = False
logdir = root / 'build/web-projection-mutations'
logdir.mkdir(parents=True,exist_ok=True)
preset = 'testing-release' if number == 3 else 'testing-web' if number == 5 else 'testing'
builddir = 'build/Testing-Release' if number == 3 else 'build/wasm/testing' if number == 5 else 'build/Testing'
build = ['cmake','--build','--preset',preset,'--target','LokaWebCounter' if number == 5 else 'LokaWebTests','-j','6']
node = shutil.which('node') or os.environ.get('EMSDK_NODE')
command = [node,'tests/web/driver/run.mjs',builddir+'/web'] if number == 5 else [builddir+'/web/LokaWebTests']
if number == 5 and not any(shutil.which(c) for c in ['chromium','chromium-browser','google-chrome']) and not os.environ.get('CHROMIUM'):
    print('mutation 5: survived; driver-only [skip] browser driver: no chromium; dirty before=%d after=%d' % (before,dirty()))
    print('command: node tests/web/driver/run.mjs build/wasm/testing/web')
    sys.exit(0)
try:
    baseline_build = run(build)
    if baseline_build.returncode: raise RuntimeError(baseline_build.stdout)
    baseline = run(command)
    if baseline.returncode: raise RuntimeError('baseline failed: '+baseline.stdout)
    path.write_text(source.replace(old,new,1))
    compiled = run(build)
    if compiled.returncode: raise RuntimeError('invalid mutant is not a kill: '+compiled.stdout)
    result = run(command)
    killed = result.returncode != 0
    (logdir / ('%d.log'%number)).write_text(result.stdout)
finally:
    path.write_bytes(original)
    restored_build = run(build)
    restored = run(command)
    after = dirty()
    print('mutation %d: %s; dirty before=%d after=%d' % (number,'killed' if killed else 'survived',before,after))
    if restored_build.returncode or restored.returncode or before != after:
        raise RuntimeError('restoration failed: '+restored_build.stdout+restored.stdout)
if not killed: sys.exit(1)
