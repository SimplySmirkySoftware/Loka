import test from 'node:test';
import assert from 'node:assert/strict';
import {setImmediate} from 'node:timers/promises';
import {grantTrust} from './suite.cjs';

for (const mode of ['late grant', 'no grant', 'early grant', 'command failure', 'subscription failure', 'ready failure']) {
  test('suite trust: ' + mode, async t => {
    t.mock.timers.enable({apis: ['setTimeout', 'Date'], now: 0});
    let grant, prepare, disposed = 0, readyAt, settled = false;
    const preparation = new Promise(resolve => { prepare = resolve; });
    const vscode = {
      workspace: {onDidGrantWorkspaceTrust(callback) {
        if (mode === 'subscription failure') throw new Error(mode);
        grant = callback;
        return {dispose() { disposed++; }};
      }},
      commands: {async executeCommand(command) {
        assert.equal(command, 'workbench.trust.manage');
        if (mode !== 'subscription failure') assert.equal(typeof grant, 'function');
        await preparation;
        if (mode === 'command failure') throw new Error(mode);
      }}
    };
    const timers = new Set();
    const clock = {
      setTimeout(callback, ms) { const timer = setTimeout(callback, ms); timers.add(timer); return timer; },
      clearTimeout(timer) { clearTimeout(timer); timers.delete(timer); }
    };
    const outcome = grantTrust(vscode, async () => {
      if (mode === 'ready failure') throw new Error(mode);
      readyAt = Date.now();
    }, clock).then(() => { settled = true; }, error => { settled = true; return error; });
    if (mode === 'early grant') grant();
    t.mock.timers.tick(30000);
    await setImmediate();
    assert.equal(settled, false);
    assert.equal(readyAt, undefined);
    prepare();
    await setImmediate();
    if (mode.endsWith('failure')) {
      assert.match((await outcome).message, new RegExp(mode));
    } else {
      assert.equal(readyAt, 30000);
      t.mock.timers.tick(19999);
      await setImmediate();
      if (mode !== 'early grant') assert.equal(settled, false);
      if (mode === 'late grant') grant();
      if (mode === 'no grant') t.mock.timers.tick(1);
      const error = await outcome;
      if (mode === 'no grant') assert.match(error.message, /Trust grant timed out/);
      else assert.equal(error, undefined);
    }
    assert.equal(timers.size, 0);
    assert.equal(disposed, mode === 'subscription failure' ? 0 : 1);
    t.mock.timers.tick(20000);
    await setImmediate(); // A cancelled timer must not create a later unhandled rejection.
  });
}
