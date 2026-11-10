import test, {mock} from 'node:test';
import assert from 'node:assert/strict';
import {posix} from 'node:path';
import {createPreview} from '../src/preview.ts';
import type {PreviewPanel, PreviewPorts} from '../src/preview.ts';

type MutablePorts = {-readonly [K in keyof PreviewPorts<() => void>]: PreviewPorts<() => void>[K]};
function fixture(trusted = true, folder: string | null = '/workspace', present: boolean | '.js' | '.wasm' = true,
  configure: (ports: MutablePorts) => void = () => {}) {
  let serial = 0, directory = 'first', html = '', assignments = 0;
  const errors: string[] = [], faults: unknown[] = [];
  let live: 'live' | 'closed' = 'live';
  const hooks = {options: () => {}, html: () => {}};
  let options = {enableScripts: false, localResourceRoots: [] as readonly string[]};
  const nativeUse = () => assert.equal(live, 'live', 'native use after disposal');
  const posts: unknown[] = [], output: string[] = [], status: string[] = [], reads: string[] = [];
  const timers = new Set<() => void>(), history: (() => void)[] = [];
  const panel = {
    get options() { nativeUse(); return options; },
    set options(value) { nativeUse(); options = value; hooks.options(); },
    get html() { nativeUse(); return html; },
    set html(value: string) { nativeUse(); html = value; ++assignments; hooks.html(); },
    postMessage(message: unknown) { nativeUse(); posts.push(message); },
  };
  const adapter: {-readonly [K in keyof PreviewPanel]: PreviewPanel[K]} = {
    install(prepared) {
      if (live === 'closed') return 'closed';
      panel.options = prepared.options;
      if (isClosed()) return 'closed';
      panel.html = prepared.html;
      return isClosed() ? 'closed' : 'installed';
    },
    postDispose(id) {
      if (live === 'closed') return 'closed';
      panel.postMessage({id, type: 'dispose'}); return 'posted';
    },
    revoke() { live = 'closed'; },
  };
  const isClosed = () => live === 'closed';
  const ports: MutablePorts = {
    admitted: () => trusted, folder: () => folder ?? undefined,
    setting: name => { reads.push(name); return directory + '/Demo.js'; },
    resolve: posix.resolve, directory: posix.dirname, stem: file => posix.basename(file, '.js'),
    existsSync: file => { reads.push(file); return typeof present === 'boolean' ? present : !file.endsWith(present); },
    asWebviewUri: file => 'https://resource.invalid' + file, cspSource: "'self'",
    identity: () => 'id-' + ++serial,
    clock: {
      after(ms, callback) { assert.equal(ms, 2000); timers.add(callback); history.push(callback); return callback; },
      cancel(timer) { timers.delete(timer); },
    },
    fault: error => { faults.push(error); },
    error: message => errors.push(message), output: message => output.push(message), status: message => status.push(message),
  };
  configure(ports);
  const preview = createPreview(adapter, ports);
  const state = () => Object.getOwnPropertyDescriptor(preview, 'state')!.value;
  const receive = (type: string, extra = {}, id = state().id) => preview.receive({id, type, ...extra});
  return {preview, panel, adapter, hooks, faults, nativeDispose: () => { adapter.revoke(); preview.close(); }, ports, posts, output, errors, status, timers, history, reads, state, receive,
    assignments: () => assignments, trust: () => { trusted = true; },
    directory: () => { directory = 'second'; }};
}

test('returned Preview has no open member', () => {
  const f = fixture();
  assert.equal('open' in f.preview, false);
});

test('RUNNING reload waits, posts once, times out at 2 s', () => {
  const f = fixture(); f.receive('started', {status: 0});
  f.preview.reload(); f.preview.reload();
  assert.equal(f.assignments(), 1); assert.equal(f.posts.length, 1); assert.equal(f.timers.size, 1);
  [...f.timers][0]();
  assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 0);
  assert.match(f.output[0], /did not acknowledge completion or absence/);
  assert.match(f.status.at(-1)!, /2 s/);
});
test('LOADING and FAILED reload wait for disposal; error rejects late started', () => {
  for (const failed of [false, true]) {
    const f = fixture();
    if (failed) {
      f.receive('error', {reason: 'load failed'});
      const before = f.state(); f.receive('started', {status: 0});
      assert.equal(f.state(), before); assert.equal(f.state().phase.kind, 'FAILED');
    }
    f.preview.reload();
    assert.equal(f.posts.length, 1); assert.equal(f.assignments(), 1);
  }
});
test('completed and absent replace immediately and cancel timer', () => {
  for (const acknowledgement of ['completed', 'absent']) {
    const f = fixture(); f.preview.reload(); f.receive(acknowledgement);
    assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 0);
    assert.equal(f.output[0], 'Previous document: ' + acknowledgement);
  }
});
test('close cancels timer and all late entries are inert', () => {
  const f = fixture(); f.preview.reload(); f.preview.close();
  assert.equal(f.timers.size, 0);
  const state = f.state(), reports = f.status.length;
  f.receive('completed'); f.preview.reload(); f.history[0]();
  assert.equal(f.state(), state); assert.equal(f.assignments(), 1); assert.equal(f.status.length, reports);
});
test('cancelled callback identity cannot replace a newer disposing document', () => {
  const f = fixture(); f.preview.reload(); const old = f.history[0];
  f.receive('completed'); f.preview.reload(); old();
  assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 1);
});
test('old document completed cannot finish new disposal', () => {
  const f = fixture(); const old = f.state().id;
  f.preview.reload(); f.receive('completed'); f.preview.reload();
  f.receive('completed', {}, old);
  assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 1);
});
test('phase filter preserves state identity and reports incompatible messages', () => {
  const f = fixture(); f.receive('started', {status: 0});
  const running = f.state(); f.receive('started', {status: 1});
  assert.equal(f.state(), running); assert.equal(f.output.length, 1);
  f.receive('completed'); const ended = f.state(); f.receive('completed');
  assert.equal(f.state(), ended); assert.equal(f.output.length, 2);
  const g = fixture(false); const message = g.state(); g.receive('error', {reason: 'late'});
  assert.equal(g.state(), message); assert.equal(g.output.length, 1);
});
test('natural completion reload replaces without post or timer', () => {
  const f = fixture(); f.receive('completed'); f.preview.reload();
  assert.equal(f.assignments(), 2); assert.equal(f.posts.length, 0); assert.equal(f.timers.size, 0);
});
test('trust is checked each replacement, and MESSAGE reload is immediate', () => {
  const f = fixture(false);
  assert.equal(f.panel.options.enableScripts, false); assert.doesNotMatch(f.panel.html, /<script/);
  assert.equal(f.reads.length, 0);
  f.trust(); f.preview.reload();
  assert.equal(f.panel.options.enableScripts, true); assert.match(f.panel.html, /<script/);
});
test('missing folder or either artifact renders MESSAGE; both existence checks run', () => {
  for (const missing of [false, '.js', '.wasm'] as const) {
    const f = fixture(true, '/workspace', missing);
    assert.equal(f.state().phase.kind, 'MESSAGE');
    assert.equal(f.reads.length, 3);
  }
  const g = fixture(true, null);
  assert.equal(g.panel.options.enableScripts, false);
  assert.equal(g.reads.length, 0);
});
test('replacement derives root and both URIs from current settings synchronously', () => {
  const f = fixture(); f.directory(); f.receive('completed'); f.preview.reload();
  assert.deepEqual(f.panel.options.localResourceRoots, ['/workspace/second']);
  assert.match(f.panel.html, /https:\/\/resource.invalid\/workspace\/second\/Demo.js/);
  assert.match(f.panel.html, /https:\/\/resource.invalid\/workspace\/second\/Demo.wasm/);
  assert.equal(f.reads.length, 6);
});
test('malformed messages are ignored; start statuses 1 and 2 are classified', () => {
  const f = fixture(); const before = f.state();
  for (const m of [null, 7, {}, {id: before.id, type: 'started', status: 3},
    {id: before.id, type: 'error', reason: 1}, {id: before.id, type: ['completed']},
    {id: before.id, type: {toString() { throw new Error('malformed'); }}}, {id: before.id, type: 'other'}]) f.preview.receive(m);
  assert.equal(f.state(), before); assert.equal(f.output.length, 0);
  f.receive('started', {status: 1}); assert.equal(f.status.at(-1), 'Windowless');
  const g = fixture(); g.receive('started', {status: 2}); assert.equal(g.state().phase.kind, 'FAILED');
});

test('Build lifetime spans Preview close and reopen; second starts stay refused', async () => {
  let f = fixture(), calls = 0;
  let resolve!: (execution: object) => void;
  const {createBuild} = await import('../src/build.ts');
  const newBuild = () => createBuild({admitted: () => true,
    execute() { ++calls; return new Promise<object>(yes => { resolve = yes; }); },
    reload: () => f.preview.reload(), report: () => {}, fault: () => {}});
  let build = newBuild();
  build.start(); build.start(); assert.equal(calls, 1);
  f.preview.close(); f = fixture(); build.start(); assert.equal(calls, 1);
  const execution = {}; resolve(execution); await Promise.resolve();
  build.start(); assert.equal(calls, 1);
  build.ended(execution, 0);
  assert.equal(f.posts.length, 1); assert.equal(f.assignments(), 1);
});


test('failed start reports once for started 2 followed by error', () => {
  const f = fixture();
  f.receive('started', {status: 2}); f.receive('error', {reason: 'Module start failed'});
  assert.deepEqual(f.errors, ['Module start failed']);
});

for (const path of ['acknowledgement', 'timeout']) {
  for (const action of ['close', 'nested acknowledgement']) {
    test(`${path} output reentry: ${action}`, () => {
      const f = fixture(); f.preview.reload(); const html = f.panel.html;
      const original = f.ports.output;
      f.ports.output = message => {
        f.ports.output = original;
        if (action === 'close') f.preview.close(); else f.receive('completed');
      };
      if (path === 'timeout') f.history[0](); else f.receive('completed');
      assert.equal(f.assignments(), 2);
      assert.equal(f.state().phase.kind, action === 'close' ? 'CLOSED' : 'LOADING');
      if (action === 'close') { assert.notEqual(f.panel.html, html); assert.equal(f.status.at(-1), 'CLOSED'); }
    });
  }
}
test('error callback close follows FAILED status', () => {
  const f = fixture(); f.ports.error = () => f.preview.close();
  f.receive('error', {reason: 'failure'});
  assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
});
test('DISPOSING status callback close follows dispose post', () => {
  const f = fixture(), original = f.ports.status;
  f.ports.status = message => { original(message); if (message === 'DISPOSING') f.preview.close(); };
  f.preview.reload();
  assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.posts.length, 1); assert.equal(f.timers.size, 0);
});
for (const port of ['admitted', 'folder', 'identity', 'setting', 'resolve', 'existsSync', 'directory', 'asWebviewUri', 'stem'] as const) {
  test(`replacement ${port} callback close follows installed document`, () => {
    const f = fixture(); f.receive('completed'); const html = f.panel.html, options = f.panel.options;
    const original = f.ports[port];
    Object.assign(f.ports, {[port]: (...args: never[]) => {
      f.preview.close(); return (original as (...args: never[]) => unknown)(...args);
    }});
    f.preview.reload();
    assert.equal(f.state().phase.kind, 'CLOSED'); assert.notEqual(f.panel.html, html);
    assert.notEqual(f.panel.options, options);
    assert.equal(f.assignments(), 2);
  });
}
test('timer creation callback close follows disposal and cancels timer', () => {
  const f = fixture(), original = f.ports.clock.after;
  f.ports.clock.after = (ms, callback) => { f.preview.close(); return original(ms, callback); };
  f.preview.reload(); assert.equal(f.state().phase.kind, 'CLOSED');
  assert.equal(f.timers.size, 0); assert.equal(f.posts.length, 1);
});
test('timer cancellation callback close cannot be overwritten', () => {
  const f = fixture(); f.preview.reload(); const original = f.ports.clock.cancel;
  f.ports.clock.cancel = timer => { f.ports.clock.cancel = original; original(timer); f.preview.close(); };
  f.receive('completed'); assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
});
for (const property of ['options', 'html'] as const) {
  test(`panel ${property} setter close stops replacement`, () => {
    const f = fixture(); f.receive('completed'); const html = f.panel.html;
    f.hooks[property] = () => f.nativeDispose();
    f.preview.reload(); assert.equal(f.state().phase.kind, 'CLOSED');
    assert.equal(f.assignments(), property === 'options' ? 1 : 2);
    assert.equal(f.status.at(-1), 'CLOSED');
    assert.throws(() => f.panel.html, /native use after disposal/);
  });
}
test('replacement status close preserves terminal timeout status', () => {
  const f = fixture(); f.preview.reload(); const original = f.ports.status;
  f.ports.status = message => { original(message); if (message === 'Waiting for startup response') f.preview.close(); };
  f.history[0](); assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
});

test('status callback may close unconditionally without recursive close', () => {
  const f = fixture(); f.ports.status = () => f.preview.close();
  f.receive('started', {status: 0}); assert.equal(f.state().phase.kind, 'CLOSED');
});

test('reload retained for started, error, completed posted from clock.after', () => {
  for (const type of ['started', 'error', 'completed']) {
    const f = fixture(); const original = f.ports.clock.after;
    f.ports.clock.after = (ms, callback) => {
      f.receive(type, {status: 0, reason: 'failure'}); return original(ms, callback);
    };
    f.preview.reload();
    assert.equal(f.posts.length, 1, type + ': reload must post disposal');
  }
});

test('initial install: outer-before, outer-after, nested', () => {
  const prototype = Object.getPrototypeOf(fixture().preview);
  const handle = prototype.handle;
  let owner!: ReturnType<typeof createPreview>;
  const capture = mock.method(prototype, 'handle', function (this: typeof owner, event: unknown) {
    owner = this;
    return handle.call(this, event);
  });
  const trace: string[] = [];
  try {
    const f = fixture(true, '/workspace', true, ports => {
      ports.admitted = () => {
        trace.push('outer-before'); owner.close(); trace.push('outer-after'); return true;
      };
      const status = ports.status;
      ports.status = message => { status(message); if (message === 'CLOSED') trace.push('nested'); };
    });
    assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
    assert.deepEqual(f.status, ['Waiting for startup response', 'CLOSED']);
    assert.equal(f.assignments(), 1); assert.equal(f.posts.length, 0);
    assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.timers.size, 0);
  } finally { capture.mock.restore(); }
});

for (const door of ['cancel', 'nested RELOAD']) {
  test(`${door}: outer-before, outer-after, nested`, () => {
    const f = fixture(), trace: string[] = [];
    const queueClose = () => { trace.push('outer-before'); f.preview.close(); trace.push('outer-after'); };
    const status = f.ports.status;
    f.ports.status = message => { status(message); if (message === 'CLOSED') trace.push('nested'); };
    if (door === 'cancel') {
      f.preview.reload();
      const cancel = f.ports.clock.cancel;
      f.ports.clock.cancel = timer => { cancel(timer); queueClose(); };
      f.receive('completed');
      assert.equal(f.assignments(), 2); assert.equal(f.posts.length, 1);
    } else {
      const install = f.adapter.install;
      f.adapter.install = prepared => {
        f.adapter.install = install;
        trace.push('outer-before'); f.preview.reload(); trace.push('outer-after'); return install(prepared);
      };
      f.ports.status = message => { status(message); if (message === 'DISPOSING') trace.push('nested'); };
      f.receive('completed'); f.preview.reload();
      assert.equal(f.assignments(), 2); assert.equal(f.posts.length, 1);
    }
    assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
    assert.equal(f.timers.size, door === 'nested RELOAD' ? 1 : 0);
  });
}

test('Preview -> Build -> Preview: outer-before, outer-after, nested', async () => {
  const {createBuild} = await import('../src/build.ts');
  const f = fixture(), trace: string[] = []; let executions = 0;
  const build = createBuild({
    admitted() { f.receive('started', {status: 0}); return true; },
    execute() { ++executions; return new Promise<object>(() => {}); },
    reload() {}, report() {}, fault() {},
  });
  f.ports.output = () => trace.push('nested');
  const status = f.ports.status;
  f.ports.status = message => {
    status(message);
    if (message === 'Running') { trace.push('outer-before'); build.start(); trace.push('outer-after'); }
  };
  f.receive('started', {status: 0});
  assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
  assert.equal(executions, 1); assert.equal(f.assignments(), 1);
  assert.equal(f.posts.length, 0); assert.equal(f.timers.size, 0);
});

test('install during DISPOSING replacement queues RELOAD for the new id', () => {
  const f = fixture(); f.preview.reload(); const old = f.state().id;
  const install = f.adapter.install;
  f.adapter.install = page => { f.preview.reload(); return install(page); };
  f.receive('completed');
  assert.notEqual(f.state().id, old);
  assert.deepEqual(f.posts, [{id: old, type: 'dispose'}, {id: f.state().id, type: 'dispose'}]);
  assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 1); assert.equal(f.history.length, 2);
});

for (const type of ['started', 'error', 'completed']) {
  test(`clock.after ${type}: disposal precedes queued message and retains reload`, () => {
    const f = fixture(), trace: string[] = [], id = f.state().id;
    const after = f.ports.clock.after, post = f.adapter.postDispose, install = f.adapter.install;
    f.ports.clock.after = (ms, callback) => { f.receive(type, {status: 0, reason: 'failure'}); return after(ms, callback); };
    f.adapter.postDispose = id => { trace.push('postDispose'); return post(id); };
    f.adapter.install = page => { trace.push('install'); return install(page); };
    f.ports.output = message => trace.push(message);
    f.preview.reload();
    assert.deepEqual(f.posts, [{id, type: 'dispose'}]);
    assert.deepEqual(trace, type === 'completed' ? ['postDispose', 'Previous document: completed', 'install']
      : ['postDispose', 'Ignored ' + type + ' in DISPOSING']);
    assert.equal(f.timers.size, type === 'completed' ? 0 : 1);
    if (type !== 'completed') f.receive('completed');
    assert.equal(f.assignments(), 2); assert.equal(f.timers.size, 0);
  });
}

test('capability absent at install entry closes without success or timeout status', () => {
  const f = fixture(); f.preview.reload(); f.adapter.revoke();
  f.history[0]();
  assert.equal(f.assignments(), 1); assert.equal(f.timers.size, 0);
  assert.equal(f.status.at(-1), 'CLOSED'); assert.equal(f.state().phase.kind, 'CLOSED');
  assert.deepEqual(f.faults, []);
});

test('queued RELOAD ahead of native CLOSE gets postDispose closed: zero native posts and timers', () => {
  const f = fixture();
  f.ports.status = message => {
    if (message === 'Running') { f.preview.reload(); f.nativeDispose(); }
  };
  f.receive('started', {status: 0});
  assert.equal(f.state().phase.kind, 'CLOSED');
  assert.equal(f.posts.length, 0); assert.equal(f.timers.size, 0); assert.equal(f.assignments(), 1);
  assert.deepEqual(f.faults, []);
});

test('postDispose closed ends current handler without waiting for a CLOSE event', () => {
  const f = fixture(); f.adapter.revoke(); f.preview.reload();
  assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
  assert.equal(f.timers.size, 0); assert.equal(f.posts.length, 0);
});

for (const setter of ['options', 'html'] as const) {
  test(`timeout destruction in ${setter} returns closed without installed or timeout status`, () => {
    const f = fixture(); f.preview.reload(); const before = f.status.length;
    f.hooks[setter] = () => f.nativeDispose();
    f.history[0]();
    assert.deepEqual(f.status.slice(before), ['CLOSED']);
    assert.equal(f.assignments(), setter === 'options' ? 1 : 2);
    assert.equal(f.timers.size, 0); assert.deepEqual(f.faults, []);
  });
}

test('Preview terminal head ignores queued valid MESSAGE, RELOAD, CLOSE and fresh doors', () => {
  const f = fixture(); f.preview.reload();
  const cancel = f.ports.clock.cancel;
  f.ports.clock.cancel = timer => {
    cancel(timer); f.receive('started', {status: 0}); f.preview.reload(); f.preview.close();
  };
  f.preview.close();
  const effects = [f.assignments(), f.status.length, f.output.length, f.errors.length, f.history.length, f.posts.length];
  f.receive('completed'); f.preview.reload(); f.preview.close(); f.history[0]();
  assert.deepEqual([f.assignments(), f.status.length, f.output.length, f.errors.length, f.history.length, f.posts.length], effects);
  assert.equal(f.output.length, 0); assert.equal(f.assignments(), 1); assert.equal(f.timers.size, 0);
});

for (const path of ['timeout output', 'CLOSE cancellation', 'CLOSED status', 'partial options']) {
  test(`fault during ${path}: revoke, release once, cleanup reentry inert, preserve original`, () => {
    const f = fixture(), original = {fault: path};
    f.preview.reload(); let cancellations = 0, revocations = 0;
    const cancel = f.ports.clock.cancel, revoke = f.adapter.revoke;
    f.adapter.revoke = () => {
      ++revocations; revoke(); f.preview.reload(); f.preview.close();
      throw new Error('cleanup must not replace primary');
    };
    f.ports.clock.cancel = timer => {
      ++cancellations;
      if (path === 'CLOSE cancellation') throw original;
      cancel(timer);
    };
    if (path === 'timeout output') f.ports.output = () => { f.preview.reload(); throw original; };
    if (path === 'CLOSED status') f.ports.status = message => { if (message === 'CLOSED') throw original; };
    if (path === 'partial options') f.hooks.options = () => { f.preview.reload(); throw original; };
    if (path === 'timeout output' || path === 'partial options') {
      f.history[0](); assert.deepEqual(f.faults, [original]); assert.equal(f.faults[0], original);
    } else {
      assert.throws(() => f.preview.close(), error => error === original);
      assert.deepEqual(f.faults, []);
    }
    assert.equal(revocations, 1); assert.equal(cancellations, 1);
    assert.equal(f.timers.size, path === 'CLOSE cancellation' ? 1 : 0);
    const effects = [f.assignments(), f.status.length, f.output.length];
    f.preview.reload(); f.receive('completed'); f.preview.close(); f.history[0]();
    assert.deepEqual([f.assignments(), f.status.length, f.output.length], effects);
    assert.equal(cancellations, 1); assert.equal(revocations, 1);
    assert.throws(() => f.panel.html, /native use after disposal/);
  });
}

test('timer fault reports throw undefined exactly once', () => {
  const f = fixture(); f.preview.reload(); f.ports.output = () => { throw undefined; };
  f.history[0](); f.history[0](); assert.deepEqual(f.faults, [undefined]); assert.equal(f.timers.size, 0);
});
