import test from 'node:test';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {dirname, join} from 'node:path';
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
  assert.deepEqual([...f.commands.keys()], ['loka.preview.open', 'loka.preview.build', 'loka.preview.reload', 'loka.preview.configure', 'loka.preview.selectExample']);
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
    new f.api.ProcessExecution('cmake', ['--build', f.root, '--target', 'TargetA'], {cwd: f.root})));
  const execution = {}; f.resolve(execution); await Promise.resolve(); f.ended(execution);
  assert.equal(p.posts.length, 2);
});
test('selected JS subdirectory is the exact resource root, not the build directory', t => {
  const f = fixture(t); f.command('open');
  assert.deepEqual((f.panels[0].options as {localResourceRoots: {fsPath: string}[]})
    .localResourceRoots.map(u => u.fsPath), [join(f.root, 'module')]);
  displayed(f, 0, 1);
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
  b.receive('completed'); displayed(f, 0, 2, b);
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
    f.hooks.status = text => { if (!text.startsWith('Loka: Built ')) throw original; };
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
    const doors = createPreview(a, {admitted: () => true, folder: () => f.root, prepare: () => ({directory: f.root, app: f.apps[0]}),
      resolve: join, directory: dirname, existsSync: () => true,
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
    f.hooks.status = text => { if (!text.startsWith('Loka: Built ')) throw new Error('status failure'); };
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
    assert.equal(f.output.length, failure === 'subscription' ? 0 : 1, 'released output cannot report its own disposal failure');
    const reports = f.statuses.length;
    p.receive('completed'); f.command('reload');
    assert.equal(p.htmlWrites, 1); assert.equal(p.posts.length, 1);
    assert.equal(f.statuses.length, reports, 'closed owner is inert');
    assert.doesNotThrow(() => f.activation.dispose());
    assert.ok(f.subscriptions.every(s => s.attempts === 1), 'second dispose does not retry releases');
    assert.equal(p.disposeAttempts, 1); assert.equal(cancel.mock.callCount(), 1);
    f.hooks.status = () => {}; f.hooks.nativeDispose = () => {};
    assert.equal(f.panels.length, 1, 'activation slot was cleared without opening another panel');
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

const settle = async () => { for (let i = 0; i < 4; ++i) await Promise.resolve(); };
function displayed(f: ReturnType<typeof fixture>, index: number, writes: number, panel = f.panels.at(-1)!, directory = f.root) {
  const app = f.apps[index];
  assert.equal(panel.htmlWrites, writes);
  assert.match(panel.html, new RegExp('window\\["' + app.exportName + '"\\]'));
  assert.ok(panel.html.includes('https://resource.invalid' + join(directory, app.js) + '?v='));
  assert.ok(panel.html.includes('https://resource.invalid' + join(directory, app.js.replace(/\.js$/, '.wasm')) + '?v='));
  assert.deepEqual((panel.options as {localResourceRoots: {fsPath: string}[]}).localResourceRoots.map(u => u.fsPath), [dirname(join(directory, app.js))]);
}
async function choose(f: ReturnType<typeof fixture>, index: number) {
  const pending = f.command('selectExample');
  f.picks.at(-1)!.resolve(f.picks.at(-1)!.items[index]); await settle();
  const write = f.writes.at(-1)!;
  assert.equal(write.key, 'loka.preview.example:' + f.folder.uri.toString());
  write.commit(); write.resolve(); await pending;
}
test('Select trust first, cancellation, no panel and persistence failure are contained', async t => {
  const f = fixture(t);
  f.trust(false);
  f.api.workspace.getConfiguration = () => { assert.fail('untrusted catalog/settings read'); };
  await f.command('selectExample'); f.command('build'); f.command('configure'); f.command('open');
  assert.equal(f.picks.length, 0); assert.equal(f.tasks.length, 0); assert.equal(f.writes.length, 0);
  assert.match(f.errors[0], /Trust/);
});
test('Select cancellation and failed save leave selection and preview unchanged; no panel stays absent', async t => {
  const f = fixture(t);
  let pending = f.command('selectExample'); f.picks[0].resolve(undefined); await pending;
  assert.equal(f.writes.length, 0); assert.equal(f.panels.length, 0);
  await choose(f, 1); assert.equal(f.panels.length, 0);
  f.command('open'); displayed(f, 1, 1);
  pending = f.command('selectExample'); f.picks.at(-1)!.resolve(f.picks.at(-1)!.items[0]); await settle();
  f.writes.at(-1)!.reject('disk'); await pending;
  displayed(f, 1, 1); assert.equal(f.panels[0].posts.length, 0); assert.match(f.errors.at(-1)!, /disk/);
});
for (const stage of ['picker', 'save']) for (const outcome of ['success', 'reject']) {
  test(`Select ${stage} ${outcome} after activation disposal touches no disposed UI`, async t => {
    const f = fixture(t); f.command('open');
    const pending = f.command('selectExample');
    if (stage === 'save') { f.picks[0].resolve(f.picks[0].items[1]); await settle(); }
    f.activation.dispose(); const statuses = f.statuses.length, output = f.output.length;
    f.hooks.error = () => Promise.reject(new Error('late notification unavailable'));
    if (stage === 'picker') {
      if (outcome === 'reject') f.picks[0].reject('late picker');
      else { f.picks[0].resolve(f.picks[0].items[1]); await settle(); f.writes[0].commit(); f.writes[0].resolve(); }
    } else if (outcome === 'reject') f.writes[0].reject('late save');
    else { f.writes[0].commit(); f.writes[0].resolve(); }
    await pending;
    assert.equal(f.statuses.length, statuses); assert.equal(f.output.length, output);
    assert.equal(f.panels[0].htmlWrites, 1); assert.equal(f.panels[0].posts.length, 0);
    assert.equal(f.errors.length, outcome === 'reject' ? 1 : 0);
  });
}
test('Select contains reload fault and rejecting error notification', async t => {
  const f = fixture(t); f.command('open');
  f.hooks.status = () => { throw new Error('reload fault'); };
  f.hooks.error = () => Promise.reject(new Error('notification failed'));
  await choose(f, 1);
  assert.equal(f.panels[0].disposeAttempts, 1);
  assert.match(f.errors[0], /reload fault/); assert.equal(f.writes.length, 1);
  f.hooks.status = () => {}; f.command('reload'); assert.equal(f.panels[0].htmlWrites, 1);
});
for (const finish of ['ack', 'timeout']) {
  test(`Select while DISPOSING uses latest selection at ${finish}, one replacement and original deadline`, async t => {
    t.mock.timers.enable({apis: ['setTimeout']});
    const f = fixture(t); f.command('open'); f.command('reload'); t.mock.timers.tick(1500);
    await choose(f, 1); await choose(f, 0); await choose(f, 1);
    assert.equal(f.panels[0].posts.length, 1); displayed(f, 0, 1);
    if (finish === 'ack') f.panels[0].receive('completed'); else t.mock.timers.tick(500);
    displayed(f, 1, 2); t.mock.timers.tick(2000); displayed(f, 1, 2);
    assert.equal(f.panels[0].posts.length, 1);
  });
}
test('Select waiting loses trust; replacement refuses without reading settings', async t => {
  const f = fixture(t); f.command('open');
  const pending = f.command('selectExample'); f.picks[0].resolve(f.picks[0].items[1]); await settle();
  f.trust(false); f.api.workspace.getConfiguration = () => { assert.fail('untrusted read'); };
  f.writes[0].commit(); f.writes[0].resolve(); await pending;
  f.panels[0].receive('completed'); assert.equal(f.panels[0].htmlWrites, 2);
  assert.match(f.panels[0].html, /Trust the workspace/); assert.doesNotMatch(f.panels[0].html, /<script/);
});
test('picker captures folder key; overlapping Select follows actual commits, not invocation or settlement', async t => {
  const f = fixture(t); f.command('open');
  const first = f.command('selectExample'), second = f.command('selectExample');
  f.picks[1].resolve(f.picks[1].items[1]); await settle();
  f.picks[0].resolve(f.picks[0].items[0]); await settle();
  assert.ok(f.writes.every(w => w.key === 'loka.preview.example:' + f.folder.uri.toString()));
  f.writes[1].commit(); f.writes[0].commit();
  f.writes[0].resolve(); await second; f.writes[1].resolve(); await first;
  f.panels[0].receive('completed'); displayed(f, 1, 2);
  const late = f.command('selectExample');
  f.folders([{...f.folder, uri: f.api.Uri.file(join(f.root, 'changed'))}]);
  f.picks[2].resolve(f.picks[2].items[0]); await settle();
  assert.equal(f.writes[2].key, 'loka.preview.example:' + f.folder.uri.toString());
  f.writes[2].commit(); f.writes[2].resolve(); await late;
  f.folders([f.folder]);
  f.panels[0].receive('completed'); displayed(f, 0, 3);
});
test('Build A and Select B: B may load before A completes and A completion reloads B', async t => {
  const f = fixture(t); f.command('open'); f.command('build');
  await choose(f, 1); f.panels[0].receive('completed'); displayed(f, 1, 2);
  const execution = {}; f.resolve(execution); await settle(); f.ended(execution);
  assert.ok(f.statuses.includes('Loka: Built TargetA')); assert.equal(f.statuses.at(-1), 'Loka: DISPOSING');
  f.panels[0].receive('completed'); displayed(f, 1, 3);
  assert.equal((f.tasks[0] as {execution: {args: string[]}}).execution.args.at(-1), 'TargetA');
  f.command('build'); assert.equal((f.tasks[1] as {execution: {args: string[]}}).execution.args.at(-1), 'TargetB');
});
test('Reload during Build reads available artifacts, without waiting for task success', async t => {
  const f = fixture(t); f.command('open'); f.command('build');
  const {rmSync} = await import('node:fs'); rmSync(join(f.root, f.apps[0].js.replace('.js', '.wasm')));
  f.command('reload'); f.panels[0].receive('completed');
  assert.equal(f.panels[0].htmlWrites, 2); assert.match(f.panels[0].html, /Build the module/);
  assert.doesNotMatch(f.panels[0].html, /<script/);
});
for (const change of ['remove', 'path', 'reorder', 'directory']) {
  test(`manual ${change} during Build/disposal is read at replacement, task retains input`, async t => {
    const f = fixture(t); f.command('open'); f.command('build'); f.command('reload');
    if (change === 'remove') { await choose(f, 0); f.catalog([f.apps[1]]); }
    if (change === 'path') f.catalog([{...f.apps[0], js: f.apps[1].js, exportName: 'FactoryB'}]);
    if (change === 'reorder') f.catalog([f.apps[1], f.apps[0]]);
    if (change === 'directory') {
      f.settings.buildDirectory = 'changed'; f.catalog([f.apps[1]], join(f.root, 'changed'));
      const {mkdirSync, copyFileSync} = await import('node:fs'); mkdirSync(join(f.root, 'changed/other'));
      for (const ext of ['js', 'wasm']) copyFileSync(join(f.root, 'other/Payload.' + ext), join(f.root, 'changed/other/Payload.' + ext));
    }
    displayed(f, 0, 1); f.command('open'); displayed(f, 0, 1);
    const checkReplacement = (writes: number) => {
      if (change === 'remove') {
        assert.equal(f.panels[0].htmlWrites, writes);
        assert.ok(f.panels[0].html.includes('Selected example is absent; use Select Example'));
        assert.doesNotMatch(f.panels[0].html, /<script/);
        assert.deepEqual(f.panels[0].options, {enableScripts: false, localResourceRoots: []});
      } else displayed(f, 1, writes, f.panels[0], change === 'directory' ? join(f.root, 'changed') : f.root);
    };
    f.panels[0].receive('completed'); checkReplacement(2);
    assert.deepEqual((f.tasks[0] as {execution: {args: string[]}}).execution.args, ['--build', f.root, '--target', 'TargetA']);
    const execution = {}; f.resolve(execution); await settle(); f.ended(execution);
    if (change !== 'remove') f.panels[0].receive('completed');
    checkReplacement(3);
  });
}
test('picker may save removed identity; Build refuses Select diagnostic and Configure remains usable', async t => {
  const f = fixture(t), pending = f.command('selectExample'); f.catalog([f.apps[0]]);
  f.picks[0].resolve(f.picks[0].items[1]); await settle(); f.writes[0].commit(); f.writes[0].resolve(); await pending;
  f.command('build'); assert.equal(f.tasks.length, 0); assert.match(f.statuses.at(-1)!, /Select Example/);
  f.command('open'); assert.match(f.panels[0].html, /Select Example/);
  f.command('configure'); assert.equal(f.tasks.length, 1);
});
test('Select closes and reopens panel while saving; completion borrows current slot only', async t => {
  const f = fixture(t); f.command('open'); const a = f.panels[0];
  const pending = f.command('selectExample'); f.picks[0].resolve(f.picks[0].items[1]); await settle();
  a.destroy(); f.command('open'); displayed(f, 0, 1);
  f.writes[0].commit(); f.writes[0].resolve(); await pending;
  assert.equal(a.posts.length, 0); assert.equal(f.panels[1].posts.length, 1);
  f.panels[1].receive('completed'); displayed(f, 1, 2);
});
for (const failure of ['missing', 'malformed', 'unsupported', 'empty', 'unreadable']) {
  test(`catalog ${failure}: all consumers diagnose, Configure succeeds without reload`, async t => {
    const f = fixture(t), {rmSync, mkdirSync, writeFileSync} = await import('node:fs');
    const file = join(f.root, 'loka-web-apps.json'); rmSync(file);
    if (failure === 'unreadable') mkdirSync(file);
    else if (failure !== 'missing') writeFileSync(file, failure === 'malformed' ? '{' : JSON.stringify({version: failure === 'unsupported' ? 2 : 1, apps: []}));
    await f.command('selectExample'); assert.match(f.errors[0], new RegExp(failure)); assert.equal(f.picks.length, 0);
    f.command('build'); assert.match(f.statuses.at(-1)!, new RegExp(failure + '; run Configure')); assert.equal(f.tasks.length, 0);
    f.command('open'); assert.match(f.panels[0].html, new RegExp(failure)); assert.doesNotMatch(f.panels[0].html, /<script/);
    f.command('configure'); assert.deepEqual(f.tasks[0], new f.api.Task({type: 'loka'}, f.folder as never, 'Configure', 'Loka', new f.api.ShellExecution('configure fixture', {cwd: f.root})));
    f.settings.configureCommand = 'changed'; const execution = {}; f.resolve(execution); await settle(); f.ended(execution);
    assert.equal(f.statuses.at(-1), 'Loka: Configured'); assert.equal(f.panels[0].htmlWrites, 1); assert.equal(f.panels[0].posts.length, 0);
  });
}

test('folder admission refuses task and picker without settings; only first folder is used', async t => {
  const f = fixture(t);
  f.folders([f.folder, {...f.folder, uri: f.api.Uri.file(join(f.root, 'unused'))}]);
  f.command('open'); displayed(f, 0, 1);
  f.folders([]);
  f.api.workspace.getConfiguration = () => { assert.fail('no-folder settings read'); };
  await f.command('selectExample'); f.command('build'); f.command('configure');
  assert.equal(f.tasks.length, 0); assert.equal(f.picks.length, 0);
  assert.equal(f.statuses.at(-1), 'Loka: Open a workspace folder');
  f.command('reload'); f.panels[0].receive('completed');
  assert.match(f.panels[0].html, /Open a workspace folder/); assert.equal(f.panels[0].htmlWrites, 2);
});

test('successful Build with no panel does not create one', async t => {
  const f = fixture(t); f.command('build'); const execution = {};
  f.resolve(execution); await settle(); f.ended(execution);
  assert.equal(f.panels.length, 0); assert.equal(f.statuses.at(-1), 'Loka: Built TargetA');
});
