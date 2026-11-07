#!/usr/bin/env python3
"""One behavioral mutation; always restore exact bytes and rebuild the clean fixture."""
import os
from pathlib import Path
import re
import subprocess
import sys

root=Path(sys.argv[1]).resolve()
number=int(sys.argv[2])
build=root/os.environ.get('LOKA_WEB_MUTATION_BUILD','build/Testing-ASan')
if number == 10:
    build=root/'build/Testing-WebMutationASan'
logs=root/'build/web-mutations'
logs.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')

def dirty():
    return len(subprocess.check_output(['git','status','--porcelain','--untracked-files=all'],cwd=root).splitlines())
def command(args,label):
    with (logs/f'{number:02}-{label}.log').open('wb') as out:
        try:
            return subprocess.run(args,cwd=root,stdout=out,stderr=subprocess.STDOUT,env=env,timeout=180).returncode
        except subprocess.TimeoutExpired:
            return 124

def change(path,old,new):
    path=root/path
    original=path.read_text()
    # Formatting is irrelevant to a link-removal mutation.
    pattern=r'\s+'.join(re.escape(part) for part in old.split())
    result,count=re.subn(pattern,lambda _:new,original,count=1)
    if count!=1: raise RuntimeError('mutation anchor absent: '+old)
    originals.setdefault(path,path.read_bytes())
    path.write_text(result)

before=dirty()
originals={}
verdict='survived'
try:
    if number == 10:
        # Disable contract diagnostics so the specified dangling receiver read
        # reaches ASan, rather than stopping at the earlier teardown assertion.
        if command(['cmake', '--preset', 'testing-asan', '-B', str(build),
                    '-DLOKA_LIFECYCLE_AUDIT=OFF', '-DLOKA_COMPILE_PINS=OFF',
                    '-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g -DNDEBUG'], 'configure') != 0:
            raise RuntimeError('ASan lifetime configuration failed')
    if command(['cmake','--build',str(build),'--target','LokaWebTests','-j','8'],'baseline-build')!=0:
        raise RuntimeError('baseline build failed')
    if command([str(build/'web/LokaWebTests')],'baseline')!=0:
        raise RuntimeError('baseline test failed')
    if number==1:
        change('web/src/WebResources.cpp','WebResource *row = routes.extract(this->token_);',
               'WebResource *row = routes.find(this->token_);')
        change('web/src/WebResources.cpp','web_revoke(this->token_);',
               'web_revoke(this->token_); row = routes.extract(this->token_);')
    elif number==2:
        change('web/src/WebResources.cpp','web_revoke(this->token_);',
               'web_revoke(this->token_); web_release(this->token_);')
    elif number==3:
        change('web/src/WebResources.hpp','this->next_ = value == UINT32_MAX ? 0 : value + 1;',
               'this->next_ = value >= 3 ? 2 : value + 1;')
    elif number==4:
        change('web/src/WebSession.cpp', '#include "WebSession.hpp"',
               '#include "WebSession.hpp"\n#include "'+str(root/'tests/web/WebTestAccess.hpp')+'"')
        change('web/src/WebSession.cpp', 'WebResource *row = this->routes_.find(token);',
               'WebResource *row = loka::web::testing::Access::sceneWalk(*this, token);')
    elif number==5:
        change('web/src/WebResources.cpp','web_set_hidden(this->token_, true);','/* terminal hide removed */')
    elif number==6:
        change('web/src/WebResources.cpp',
               'if (loka::web::testing::refuseCommit()) { web_release(row->token); delete routes.extract(row->token); return false; }',
               'if (loka::web::testing::refuseCommit()) { return false; }')
    elif number==7:
        change('web/src/WebSession.cpp',
               'if (result == WebSession::NESTED) { Dispatch(session, kind, token, width, height); return result; }',
               'if (result == WebSession::NESTED) { Dispatch(session, kind, token, width, height); session.app_->tail(turn, entry); return result; }')
    elif number==8:
        change('web/src/WebIdle.cpp',
               'this->handle = 0; this->kind = loka::web::WAKE_COUNT; return true;',
               'return true;')
    elif number==9:
        change('web/src/WebSession.cpp','extra < 4 && session.app_->pending()', 'session.app_->pending()')
    elif number==10:
        change('web/src/WebSession.cpp','void WebSession::dispose() { this->quit(); }',
               'void WebSession::dispose() { this->quit(); this->execution_ = IDLE; this->shutdown(); }')
    elif number==11:
        change('web/src/WebSession.cpp','case COMPLETING: return REFUSED_COMPLETING;', 'case COMPLETING: return NESTED;')
    elif number==12:
        change('web/src/WebIdle.cpp','this->deadline_ = now + this->policy_.intervalSeconds * 1000;',
               '/* interval deadline advance removed */')
    elif number==13:
        change('web/src/WebSession.cpp','this->app_->requestQuitAtCompletion();',
               'this->quit();')
    else:
        raise RuntimeError('mutation number must be 1..13')
    if command(['cmake','--build',str(build),'--target','LokaWebTests','-j','8'],'build')!=0:
        raise RuntimeError('mutant did not compile; not counted as killed')
    result=command([str(build/'web/LokaWebTests')] + (['dispose-input'] if number == 10 else []),'test')
    verdict='killed' if result!=0 and result!=124 else 'survived'
    if number == 10 and 'AddressSanitizer: heap-use-after-free' not in (logs/'10-test.log').read_text():
        verdict='survived'
finally:
    for path,original in originals.items(): path.write_bytes(original)
    restored=command(['cmake','--build',str(build),'--target','LokaWebTests','-j','8'],'restore-build')
    after=dirty()
    print(f'mutation {number}: {verdict}; dirty before={before} after={after}',flush=True)
    if restored!=0 or before!=after: raise RuntimeError('restore verification failed')
sys.exit(0 if verdict=='killed' else 1)
