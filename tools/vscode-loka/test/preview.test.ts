import test from 'node:test';
import assert from 'node:assert/strict';
import {posix} from 'node:path';
import {createPreview} from '../src/preview.ts';
import type {PreviewPanel, PreviewPorts} from '../src/preview.ts';

function fixture(trusted = true, folder: string | null = '/workspace', present: boolean | '.js' | '.wasm' = true) {
  let serial = 0, directory = 'first', html = '', assignments = 0;
  const errors: string[] = [];
  const posts: unknown[] = [], output: string[] = [], status: string[] = [], reads: string[] = [];
  const timers = new Set<() => void>(), history: (() => void)[] = [];
  const panel: PreviewPanel = {
    options: {enableScripts: false, localResourceRoots: []},
    get html() { return html; },
    set html(value) { html = value; ++assignments; },
    postMessage(message) { posts.push(message); },
  };
  const ports: {-readonly [K in keyof PreviewPorts<() => void>]: PreviewPorts<() => void>[K]} = {
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
    error: message => errors.push(message), output: message => output.push(message), status: message => status.push(message),
  };
  const preview = createPreview(panel, ports);
  const state = () => Object.getOwnPropertyDescriptor(preview, 'state')!.value;
  const receive = (type: string, extra = {}, id = state().id) => preview.receive({id, type, ...extra});
  return {preview, panel, ports, posts, output, errors, status, timers, history, reads, state, receive,
    assignments: () => assignments, trust: () => { trusted = true; },
    directory: () => { directory = 'second'; }};
}

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
    reload: () => f.preview.reload(), report: () => {}});
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
      assert.equal(f.assignments(), action === 'close' ? 1 : 2);
      assert.equal(f.state().phase.kind, action === 'close' ? 'CLOSED' : 'LOADING');
      if (action === 'close') { assert.equal(f.panel.html, html); assert.equal(f.status.at(-1), 'CLOSED'); }
    });
  }
}
test('error callback close suppresses stale FAILED status', () => {
  const f = fixture(); f.ports.error = () => f.preview.close();
  f.receive('error', {reason: 'failure'});
  assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
});
test('DISPOSING status callback close suppresses dispose post', () => {
  const f = fixture(), original = f.ports.status;
  f.ports.status = message => { original(message); if (message === 'DISPOSING') f.preview.close(); };
  f.preview.reload();
  assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.posts.length, 0); assert.equal(f.timers.size, 0);
});
for (const port of ['admitted', 'folder', 'identity', 'setting', 'resolve', 'existsSync', 'directory', 'asWebviewUri', 'stem'] as const) {
  test(`replacement ${port} callback close preserves CLOSED and html`, () => {
    const f = fixture(); f.receive('completed'); const html = f.panel.html, options = f.panel.options;
    const original = f.ports[port];
    Object.assign(f.ports, {[port]: (...args: never[]) => {
      f.preview.close(); return (original as (...args: never[]) => unknown)(...args);
    }});
    f.preview.reload();
    assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.panel.html, html);
    assert.equal(f.panel.options, options);
  });
}
test('timer creation callback close cancels uninstalled timer', () => {
  const f = fixture(), original = f.ports.clock.after;
  f.ports.clock.after = (ms, callback) => { f.preview.close(); return original(ms, callback); };
  f.preview.reload(); assert.equal(f.state().phase.kind, 'CLOSED');
  assert.equal(f.timers.size, 0); assert.equal(f.posts.length, 0);
});
test('timer cancellation callback close cannot be overwritten', () => {
  const f = fixture(); f.preview.reload(); const original = f.ports.clock.cancel;
  f.ports.clock.cancel = timer => { f.ports.clock.cancel = original; original(timer); f.preview.close(); };
  f.receive('completed'); assert.equal(f.state().phase.kind, 'CLOSED'); assert.equal(f.status.at(-1), 'CLOSED');
});
for (const property of ['options', 'html'] as const) {
  test(`panel ${property} setter close stops replacement`, () => {
    const f = fixture(); f.receive('completed'); const html = f.panel.html;
    Object.defineProperty(f.panel, property, {set() { f.preview.close(); }});
    f.preview.reload(); assert.equal(f.state().phase.kind, 'CLOSED');
    if (property === 'options') assert.equal(f.panel.html, html);
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
