import test from 'node:test';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {join} from 'node:path';
import {wire, createPanelAdapter} from '../src/wire.ts';
import {fakeHost} from './fake.ts';
import {createPreview} from '../src/preview.ts';
import type {PreviewDoors} from '../src/preview.ts';

function fixture(t: {after(fn: () => void): void}) {
  const f = fakeHost(); const activation = wire(f.api, f.context);
  t.after(() => { f.hooks.nativeDispose = () => {}; f.hooks.release = () => {}; activation.dispose(); f.cleanup(); });
  return {...f, activation};
}
test('commands reach owners; panel options, resource roots, URI and task cwd are exact', async t => {
  const f = fixture(t);
  assert.deepEqual([...f.commands.keys()], ['loka.preview.open', 'loka.preview.build', 'loka.preview.reload']);
  f.command('reload'); assert.equal(f.panels.length, 0);
  f.command('open'); f.command('open'); const p = f.panels[0];
  assert.equal(f.panels.length, 1); assert.equal(p.reveals, 1);
  assert.deepEqual(p.creation, {enableScripts: false, retainContextWhenHidden: true, localResourceRoots: []});
  const options = p.options as {enableScripts: boolean; localResourceRoots: {fsPath: string}[]};
  assert.equal(options.enableScripts, true);
  assert.deepEqual(options.localResourceRoots.map(u => u.fsPath), [join(f.root, 'module')]);
  const files = ['Demo.js', 'Demo.wasm'].map(name => join(f.root, 'module', name));
  assert.deepEqual(p.conversions, files.map(file => pathToFileURL(file).href));
  for (const file of files) assert.ok(p.html.includes('https://resource.invalid' + file + '?v='));
  assert.match(p.html, /https:\/\/\*\.vscode-cdn.net/);
  const id = /const id = "([^"]+)"/.exec(p.html)?.[1];
  assert.ok(id);
  f.command('reload'); assert.deepEqual(p.posts, [{id, type: 'dispose'}]); p.receive('completed');
  assert.equal(p.htmlWrites, 2);
  f.command('build'); assert.equal(f.tasks.length, 1);
  assert.deepEqual(f.tasks[0], new f.api.Task({type: 'loka'}, f.folder as never, 'Build', 'Loka',
    new f.api.ShellExecution('build fixture && finish', {cwd: f.root})));
  const execution = {}; f.resolve(execution); await Promise.resolve(); f.ended(execution);
  assert.equal(p.posts.length, 2);
});
test('fresh admission blocks both owners then trust passes without rewiring', t => {
  const f = fixture(t); f.trust(false); f.command('build'); f.command('open');
  assert.equal(f.tasks.length, 0); assert.doesNotMatch(f.panels[0].html, /<script/);
  assert.match(f.panels[0].html, /Trust the workspace/);
  f.trust(true); f.command('reload'); f.command('build');
  assert.equal(f.tasks.length, 1); assert.match(f.panels[0].html, /<script/);
});
test('Open A, Build, close A, Open B: same Build reloads B and second Build stays refused', async t => {
  const f = fixture(t); f.command('open'); const a = f.panels[0]; f.command('build');
  a.destroy(); f.command('open'); const b = f.panels[1]; f.command('build');
  assert.equal(f.tasks.length, 1);
  const execution = {}; f.resolve(execution); await Promise.resolve(); f.ended(execution); f.taskEnded(execution);
  assert.equal(a.posts.length, 0); assert.equal(b.posts.length, 1);
});
for (const where of ['options', 'html', 'disposeRegistration', 'messageRegistration'] as const) {
  test(`first ${where} native destruction closes before any later write and clears current`, t => {
    const f = fixture(t); f.hooks[where] = p => p.destroy();
    assert.doesNotThrow(() => f.command('open'));
    const a = f.panels[0];
    assert.equal(a.optionsWrites, where.endsWith('Registration') ? 0 : 1);
    assert.equal(a.htmlWrites, where === 'html' ? 1 : 0);
    assert.equal(f.output.length, 0); assert.deepEqual(f.statuses, ['Loka: CLOSED'], 'no installed status after native destruction');
    assert.ok(f.subscriptions.filter(s => s.label.startsWith('panel')).every(s => !s.active));
    f.command('reload'); assert.equal(a.posts.length, 0);
    f.hooks[where] = () => {}; f.command('open'); assert.equal(f.panels.length, 2);
  });
}
test('native disposal revokes before queued reload and CLOSE', t => {
  const f = fixture(t); f.command('open'); const a = f.panels[0];
  f.hooks.status = text => { if (text === 'Loka: Running') { f.command('reload'); a.destroy(); } };
  assert.doesNotThrow(() => a.receive('started', {status: 0}));
  assert.equal(a.posts.length, 0); assert.equal(f.statuses.at(-1), 'Loka: CLOSED');
});
test('returned dispose releases every subscription and Build is behaviorally DISPOSED', async t => {
  const f = fixture(t); f.command('open'); f.command('build'); f.command('reload');
  f.activation.dispose();
  assert.ok(f.subscriptions.every(s => !s.active));
  assert.equal(f.panels[0].disposeAttempts, 1);
  const reports = f.statuses.length, execution = {};
  f.resolve(execution); await Promise.resolve(); f.ended(execution); f.taskEnded(execution); f.command('build');
  assert.equal(f.tasks.length, 1); assert.equal(f.statuses.length, reports);
});
for (const entrance of ['initial install', 'Reload', 'receive', 'timer', 'Build reload']) {
  test(`fault from ${entrance} retires once and next Open is fresh`, async t => {
    t.mock.timers.enable({apis: ['setTimeout']});
    const f = fixture(t), original = {entrance};
    if (entrance !== 'initial install') f.command('open');
    if (entrance === 'timer') f.command('reload');
    if (entrance === 'Build reload') f.command('build');
    f.hooks.status = () => { throw original; };
    const run = () => {
      if (entrance === 'initial install') f.command('open');
      if (entrance === 'Reload') f.command('reload');
      if (entrance === 'receive') f.panels[0].receive('started', {status: 0});
      if (entrance === 'timer') t.mock.timers.tick(2000);
      if (entrance === 'Build reload') f.ended(execution);
    };
    const execution = {};
    if (entrance === 'Build reload') { f.resolve(execution); await Promise.resolve(); }
    if (entrance === 'timer') assert.doesNotThrow(run); else assert.throws(run, e => e === original);
    assert.equal(f.panels[0].disposeAttempts, 1);
    assert.ok(f.subscriptions.filter(s => s.label.startsWith('panel')).every(s => !s.active));
    f.command('reload'); f.hooks.status = () => {}; f.command('open');
    assert.equal(f.panels.length, 2);
  });
}
test('async fault sink contains throwing diagnostic and rejected error notification', async t => {
  t.mock.timers.enable({apis: ['setTimeout']});
  const f = fixture(t); f.command('open'); f.command('reload');
  f.hooks.output = () => { throw new Error('diagnostic unavailable'); };
  assert.doesNotThrow(() => t.mock.timers.tick(2000));
  f.command('open'); f.hooks.error = () => Promise.reject(new Error('notification unavailable'));
  f.panels[1].receive('error', {reason: 'failed'});
  await Promise.resolve(); await Promise.resolve();
  assert.equal(f.panels[0].disposeAttempts, 1);
});

/** Direct adapter fixture exercises containment that mailbox cleanup deliberately masks. */
function adapterFixture(t: {after(fn: () => void): void}) {
  const f = fakeHost(); t.after(() => f.cleanup());
  let current: {doors: PreviewDoors; adapter: ReturnType<typeof createPanelAdapter>} | undefined;
  const faults: unknown[] = [];
  const make = () => createPanelAdapter(f.api,
    f.api.window.createWebviewPanel('loka.preview', 'Loka Preview', 2, {}),
    {get: () => current, set: value => { current = value; }}, e => { faults.push(e); });
  return {...f, make, faults, current: () => current};
}
test('retire is directly nonthrowing, idempotent, and posts no close even on native reentry', t => {
  const f = adapterFixture(t), a = f.make(); let closes = 0;
  a.connect({reload() {}, receive() {}, close() { ++closes; }});
  f.hooks.nativeDispose = p => { p.destroy(); throw new Error('native dispose'); };
  assert.doesNotThrow(() => a.retire()); a.retire(); f.panels[0].destroy();
  assert.equal(f.panels[0].disposeAttempts, 1); assert.equal(closes, 0); assert.equal(f.current(), undefined);
  assert.equal(f.faults.length, 1);
});
for (const successor of [false, true]) {
  test(`throwing first subscription still releases second and closes A; current B=${successor}`, t => {
    const f = adapterFixture(t), a = f.make(); let closes = 0;
    const timers = new Set<() => void>();
    const doors = createPreview(a, {admitted: () => true, folder: () => f.root, setting: () => 'module/Demo.js',
      resolve: join, directory: () => join(f.root, 'module'), stem: () => 'Demo', existsSync: () => true,
      asWebviewUri: file => a.asWebviewUri(file), cspSource: a.cspSource, identity: () => 'id',
      clock: {after(_ms, callback) { timers.add(callback); return callback; }, cancel(timer) { timers.delete(timer); }},
      output() {}, status() {}, error() {}, fault() {},
    }, d => a.connect({...d, reload: () => d.reload(), receive: raw => d.receive(raw), close: () => { ++closes; d.close(); }}));
    doors.reload(); assert.equal(timers.size, 1);
    const b = successor ? f.make() : undefined;
    b?.connect({reload() {}, receive() {}, close() {}});
    f.hooks.release = label => { if (label === 'panel-close') throw new Error('release'); };
    assert.doesNotThrow(() => f.panels[0].destroy());
    assert.equal(closes, 1); assert.equal(timers.size, 0);
    assert.deepEqual(f.subscriptions.slice(0, 2).map(s => s.attempts), [1, 1]);
    assert.equal(f.current()?.adapter, b);
    a.retire(); assert.equal(f.panels[0].disposeAttempts, 0);
    b?.retire();
  });
}
test('old live A retirement preserves B even when native dispose reenters', t => {
  const f = adapterFixture(t), a = f.make(), b = f.make();
  const doors = {reload() {}, receive() {}, close() { assert.fail('retirement must not close'); }};
  a.connect(doors); b.connect(doors);
  f.hooks.nativeDispose = () => assert.equal(f.current()?.adapter, b);
  a.retire(); assert.equal(f.current()?.adapter, b);
  f.hooks.nativeDispose = () => {}; b.retire();
});
test('late returned subscription uses contained release after registration disposal', t => {
  const f = adapterFixture(t), a = f.make(); let closes = 0;
  f.hooks.disposeRegistration = p => p.destroy();
  f.hooks.release = () => { throw new Error('late release'); };
  assert.doesNotThrow(() => a.connect({reload() {}, receive() {}, close() { ++closes; }}));
  assert.equal(closes, 1); assert.equal(f.faults.length, 1);
  assert.deepEqual(f.subscriptions.map(s => s.attempts), [1]);
});

for (const registration of ['disposeRegistration', 'messageRegistration'] as const) {
  test(`throwing ${registration} retires the published slot without retry`, t => {
    const f = fixture(t), original = {registration}; let registrations = 0;
    f.hooks[registration] = () => { ++registrations; throw original; };
    assert.throws(() => f.command('open'), e => e === original);
    assert.equal(registrations, 1); assert.equal(f.panels[0].optionsWrites, 0);
    assert.equal(f.panels[0].disposeAttempts, 1);
    assert.ok(f.subscriptions.filter(s => s.label.startsWith('panel')).every(s => !s.active));
    f.hooks[registration] = () => {}; f.command('open'); assert.equal(f.panels.length, 2);
  });
}
test('native retirement reentry can publish B after A was removed', t => {
  const f = adapterFixture(t), a = f.make();
  const doors = {reload() {}, receive() {}, close() { assert.fail('no retirement close'); }};
  a.connect(doors);
  let b: ReturnType<typeof createPanelAdapter> | undefined;
  f.hooks.nativeDispose = () => {
    assert.equal(f.current(), undefined); b = f.make(); b.connect(doors);
  };
  a.retire(); assert.equal(f.current()?.adapter, b); assert.equal(f.panels.length, 2);
  f.hooks.nativeDispose = () => {}; b!.retire();
});
for (const settlement of ['success', 'rejection']) {
  test(`async Build ${settlement} fault reporting contains diagnostic failure`, async t => {
    const f = fixture(t), execution = {};
    f.command('open');
    if (settlement === 'success') f.hooks.execute = () => f.ended(execution);
    f.command('build');
    f.hooks.status = () => { throw new Error('status failure'); };
    f.hooks.output = () => { throw new Error('diagnostic failure'); };
    if (settlement === 'success') f.resolve(execution); else f.reject('execution refused');
    await Promise.resolve(); await Promise.resolve();
    assert.equal(f.panels[0].disposeAttempts, settlement === 'success' ? 1 : 0);
    f.command('build'); assert.equal(f.tasks.length, 1, 'faulted Build is inert');
    f.hooks.status = () => {};
  });
}
test('processless task event reaches Build and permits the next task', async t => {
  const f = fixture(t), execution = {}; f.command('build'); f.resolve(execution); await Promise.resolve();
  f.taskEnded(execution);
  assert.equal(f.statuses.at(-1), 'Loka: Build ended without a process result');
  f.command('build'); assert.equal(f.tasks.length, 2);
});
test('strict fake refuses unknown API access', t => {
  const f = fixture(t);
  assert.throws(() => Reflect.get(f.api, 'unmodelled'), /unknown API read/);
  assert.throws(() => Reflect.set(f.api.workspace, 'unmodelled', true), /unknown API write/);
});
test('native close observes revoked capability and empty current before posting owner close', t => {
  const f = adapterFixture(t), a = f.make(); let closes = 0;
  a.connect({reload() {}, receive() {}, close() {
    ++closes;
    assert.equal(f.current(), undefined, 'slot detached before owner close');
    assert.equal(a.postDispose('id'), 'closed', 'capability revoked before owner close');
    assert.equal(a.install({options: {enableScripts: false, localResourceRoots: []}, html: ''}), 'closed');
  }});
  f.panels[0].destroy(); assert.equal(closes, 1);
  assert.equal(f.panels[0].optionsWrites, 0); assert.equal(f.panels[0].posts.length, 0);
});

for (const failure of ['native', 'subscription', 'close'] as const) {
  test(`activation shutdown contains ${failure} failure and does not repeat work`, t => {
    const cancel = t.mock.method(globalThis, 'clearTimeout');
    const f = fixture(t); f.command('open'); f.command('reload');
    const p = f.panels[0], original = new Error(failure);
    if (failure === 'native') f.hooks.nativeDispose = () => { throw original; };
    if (failure === 'subscription') f.hooks.release = label => { if (label === 'output') throw original; };
    if (failure === 'close') f.hooks.status = text => { if (text === 'Loka: CLOSED') throw original; };
    assert.doesNotThrow(() => f.activation.dispose());
    assert.ok(f.subscriptions.every(s => s.attempts === 1 && !s.active), 'every subscription released once');
    assert.equal(p.disposeAttempts, 1);
    assert.equal(cancel.mock.callCount(), 1, 'DISPOSING timer released');
    if (failure !== 'close') assert.equal(f.statuses.at(-1), 'Loka: CLOSED');
    assert.equal(f.output.length, 1, 'fault reported once');
    const reports = f.statuses.length;
    p.receive('completed'); f.command('reload');
    assert.equal(p.htmlWrites, 1); assert.equal(p.posts.length, 1);
    assert.equal(f.statuses.length, reports, 'closed owner is inert');
    assert.doesNotThrow(() => f.activation.dispose());
    assert.ok(f.subscriptions.every(s => s.attempts === 1), 'second dispose does not retry releases');
    assert.equal(p.disposeAttempts, 1); assert.equal(cancel.mock.callCount(), 1);
    f.hooks.status = () => {}; f.hooks.nativeDispose = () => {};
    f.command('open'); assert.equal(f.panels.length, 2, 'activation slot was cleared');
    f.panels[1].destroy();
  });
}
test('failed native retirement leaves later adapter writes closed', t => {
  const f = adapterFixture(t), a = f.make();
  a.connect({reload() {}, receive() {}, close() { assert.fail('retire does not close'); }});
  f.hooks.nativeDispose = () => { throw new Error('native'); };
  assert.doesNotThrow(() => a.retire());
  assert.equal(a.postDispose('id'), 'closed');
  assert.equal(a.install({options: {enableScripts: false, localResourceRoots: []}, html: ''}), 'closed');
  assert.equal(f.current(), undefined); assert.equal(f.panels[0].posts.length, 0);
});
test('fake Disposable retries a callback only after failure', t => {
  const f = fakeHost(); t.after(() => f.cleanup()); let calls = 0;
  const d = new f.api.Disposable(() => { if (++calls === 1) throw new Error('first'); });
  assert.throws(() => d.dispose(), /first/); d.dispose(); d.dispose();
  assert.equal(calls, 2);
});
test('rejecting disposal thenable reports once even when output throws', async t => {
  const f = fixture(t); f.command('open');
  let reports = 0;
  f.hooks.output = text => {
    ++reports; assert.match(text, /post rejected/); throw new Error('output unavailable');
  };
  f.hooks.postMessage = () => ({then(resolve, reject) {
    return Promise.reject(new Error('post rejected')).then(resolve, reject);
  }});
  f.command('reload');
  // Cross a full event-loop turn so node:test also observes any unhandled rejection.
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(reports, 1, 'post rejection fault reported once');
});
