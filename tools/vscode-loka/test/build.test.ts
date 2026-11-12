import test from 'node:test';
import assert from 'node:assert/strict';
import type {Operation} from '../src/build.ts';
import {createBuild} from '../src/build.ts';

function prepare(kind: Operation['kind']): Operation {
  return kind === 'BUILD' ? {kind, cwd: '/workspace', directory: '/build', target: 'Target'}
    : {kind, cwd: '/workspace', command: 'configure'};
}

function fixture() {
  let trusted = true, calls = 0, reloads = 0;
  let resolve!: (value: object) => void, reject!: (reason: unknown) => void;
  let throws = false;
  const reports: string[] = [];
  const build = createBuild({prepare, fault: () => {},
    admitted: () => trusted,
    execute() {
      ++calls;
      if (throws) throw new Error('construction');
      return new Promise<object>((yes, no) => { resolve = yes; reject = no; });
    },
    reload() { ++reloads; }, report: message => reports.push(message),
  });
  return {build, reports, calls: () => calls, reloads: () => reloads,
    resolve: (value: object) => resolve(value), reject: () => reject(new Error('execution')),
    trust: (value: boolean) => { trusted = value; }, throws: (value: boolean) => { throws = value; },
    run: () => Object.getOwnPropertyDescriptor(build, 'run')!.value};
}
const tick = async () => { await Promise.resolve(); };

test('Build admission is fresh and blocks execution', () => {
  const f = fixture(); f.trust(false); f.build.start(); assert.equal(f.calls(), 0);
  f.trust(true); f.build.start(); assert.equal(f.calls(), 1);
});
test('second Build while STARTING or RUNNING never executes again', async () => {
  const f = fixture(); f.build.start(); f.build.start(); assert.equal(f.calls(), 1);
  f.resolve({}); await tick(); f.build.start(); assert.equal(f.calls(), 1); assert.equal(f.reloads(), 0);
});
test('execution identity, success once, duplicate end ignored', async () => {
  const f = fixture(), execution = {definition: 'same'};
  f.build.start(); f.resolve(execution); await tick(); const running = f.run();
  f.build.ended({definition: 'same'}, 0); f.build.taskEnded({definition: 'same'});
  assert.equal(f.run(), running); assert.equal(f.reloads(), 0);
  f.build.ended(execution, 0); f.build.taskEnded(execution); f.build.ended(execution, 0);
  assert.equal(f.reloads(), 1); assert.deepEqual(f.reports, ['Built Target']); assert.equal(f.run().kind, 'IDLE');
});
test('nonzero, undefined, and processless ends fail without reload', async () => {
  for (const exit of [1, undefined, 'task']) {
    const f = fixture(), execution = {};
    f.build.start(); f.resolve(execution); await tick();
    if (exit === 'task') f.build.taskEnded(execution);
    else f.build.ended(execution, exit as number | undefined);
    assert.equal(f.reloads(), 0); assert.equal(f.reports.length, 1); assert.equal(f.run().kind, 'IDLE');
    f.build.start(); assert.equal(f.calls(), 2);
  }
});
test('construction throw and execution rejection report once and permit retry', async () => {
  for (const throwing of [false, true]) {
    const f = fixture(); f.throws(throwing); f.build.start();
    if (!throwing) f.reject();
    await tick();
    assert.equal(f.run().kind, 'IDLE'); assert.equal(f.reports.length, 1);
    f.throws(false); f.build.start(); assert.equal(f.calls(), 2);
  }
});
test('DISPOSED ignores resolve, reject, process end, task end, and start', async () => {
  for (const settle of ['resolve', 'reject', 'running']) {
    const f = fixture(), execution = {};
    f.build.start();
    if (settle === 'running') { f.resolve(execution); await tick(); }
    f.build.dispose(); const disposed = f.run();
    if (settle === 'resolve') f.resolve(execution);
    if (settle === 'reject') f.reject();
    await tick();
    f.build.ended(execution, 0); f.build.taskEnded(execution); f.build.start();
    assert.equal(f.run(), disposed); assert.equal(f.calls(), 1);
    assert.equal(f.reloads(), 0); assert.equal(f.reports.length, 0);
  }
});

test('early ends reconcile by identity with process precedence and permit retry', async () => {
  for (const ends of ['success', 'failure', 'undefined', 'task', 'both', 'reverse']) {
    const f = fixture(), execution = {};
    f.build.start();
    if (['task', 'both'].includes(ends)) f.build.taskEnded(execution);
    if (ends !== 'task') f.build.ended(execution, ends === 'failure' ? 1 : ends === 'undefined' ? undefined : 0);
    if (ends === 'reverse') f.build.taskEnded(execution);
    f.resolve(execution); await tick();
    const success = ['success', 'both', 'reverse'].includes(ends);
    assert.equal(f.reloads(), success ? 1 : 0, ends);
    assert.equal(f.reports.length, 1, ends);
    assert.equal(f.run().kind, 'IDLE', ends);
    f.build.taskEnded(execution); assert.equal(f.reports.length, 1);
    f.build.start(); assert.equal(f.calls(), 2);
  }
});
test('unrelated early ends leave acquired execution RUNNING', async () => {
  const f = fixture(), execution = {};
  f.build.start(); f.build.ended({}, 0); f.build.taskEnded({});
  f.resolve(execution); await tick();
  assert.equal(f.run().kind, 'RUNNING'); assert.equal(f.reloads(), 0);
  f.build.ended(execution, 0); assert.equal(f.reloads(), 1);
});
test('dispose or rejection drops early facts', async () => {
  for (const dispose of [true, false]) {
    const f = fixture(), execution = {};
    f.build.start(); f.build.ended(execution, 0); f.build.taskEnded(execution);
    if (dispose) { f.build.dispose(); f.resolve(execution); } else f.reject();
    await tick(); assert.equal(f.reloads(), 0);
    assert.equal(f.run().kind, dispose ? 'DISPOSED' : 'IDLE');
    assert.equal(f.reports.length, dispose ? 0 : 1);
  }
});

test('admission nested START is queued and launches once', () => {
  const trace: string[] = []; let calls = 0;
  const build = createBuild({prepare, fault: () => {},
    admitted() { trace.push('outer-before'); build.start(); trace.push('outer-after'); return true; },
    execute() { ++calls; return new Promise<object>(() => {}); },
    report() { trace.push('nested'); }, reload() {},
  });
  build.start();
  assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
  assert.equal(calls, 1);
});

test('admission nested DISPOSE launches current task then becomes terminal', async () => {
  let calls = 0, reloads = 0; const reports: string[] = [];
  const build = createBuild({prepare, fault() {},
    admitted() { build.dispose(); return true; },
    execute() { ++calls; return Promise.resolve({}); },
    reload() { ++reloads; }, report(message) { reports.push(message); },
  });
  build.start(); await tick(); build.start();
  assert.equal(calls, 1); assert.equal(reloads, 0); assert.deepEqual(reports, []);
});

test('execute synchronous notifications: outer-before, outer-after, nested; early success once', async () => {
  const trace: string[] = [], execution = {}; let reloads = 0;
  const build = createBuild({prepare, fault() {}, admitted: () => true,
    execute(): Promise<object> {
      trace.push('outer-before'); build.ended(execution, 0); build.taskEnded(execution); build.start();
      assert.deepEqual(Object.getOwnPropertyDescriptor(build, 'run')!.value.early, []);
      trace.push('outer-after'); return Promise.resolve(execution);
    },
    report() { trace.push('nested'); }, reload() { ++reloads; },
  });
  build.start();
  assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
  await tick(); build.ended(execution, 0); build.taskEnded(execution);
  assert.equal(reloads, 1); assert.equal(Object.getOwnPropertyDescriptor(build, 'run')!.value.kind, 'IDLE');
});

for (const settlement of ['ACQUIRED', 'REJECTED']) {
  for (const original of [{fault: settlement}, undefined]) {
    test(`${settlement} callback reports original ${String(original)} once and fail-stops`, async () => {
      const faults: unknown[] = [], execution = {}; let calls = 0, reports = 0;
      const build = createBuild({prepare, admitted: () => true,
        execute() {
          ++calls; build.ended(execution, 0);
          return settlement === 'ACQUIRED' ? Promise.resolve(execution) : Promise.reject('expected rejection');
        },
        reload() { build.start(); throw original; },
        report() { ++reports; build.start(); throw original; },
        fault(error) { faults.push(error); build.start(); build.dispose(); },
      });
      build.start(); await tick(); await tick();
      build.start(); build.ended(execution, 0); build.taskEnded(execution); build.dispose();
      assert.deepEqual(faults, [original]); assert.equal(faults[0], original);
      assert.equal(calls, 1); assert.equal(reports, 1);
    });
  }
}

test('synchronous execute rejection is queued; reporting fault is not another rejection', () => {
  const original = {}; let reports = 0, faults = 0;
  const build = createBuild({prepare, admitted: () => true, execute() { throw 'expected'; }, reload() {},
    report() { ++reports; throw original; }, fault() { ++faults; },
  });
  assert.throws(() => build.start(), error => error === original);
  build.start(); assert.equal(reports, 1); assert.equal(faults, 0);
});

test('thenable assimilation settles once asynchronously; throwing getter becomes REJECTED', async () => {
  let reloads = 0; const reports: string[] = [], execution = {};
  const build = createBuild({prepare, admitted: () => true, fault() {},
    execute() {
      build.ended(execution, 0);
      return {then(resolve: (value: object) => void, reject: (error: unknown) => void) {
        resolve(execution); reject('second'); resolve(execution);
      }} as unknown as PromiseLike<object>;
    }, reload() { ++reloads; }, report(message) { reports.push(message); },
  });
  build.start(); assert.equal(reloads, 0); await tick(); await tick();
  assert.equal(reloads, 1); assert.deepEqual(reports, ['Built Target']);
  const other = createBuild({prepare, admitted: () => true, fault() {}, reload() {},
    execute() { return Object.defineProperty({}, 'then', {get() { throw 'getter'; }}) as PromiseLike<object>; },
    report(message) { reports.push(message); },
  });
  other.start(); await tick(); assert.deepEqual(reports, ['Built Target', 'Build failed: getter']);
});

test('duplicate early process and task ends publish once, process wins in either order', async () => {
  for (const taskFirst of [true, false]) {
    const f = fixture(), execution = {};
    f.build.start();
    if (taskFirst) { f.build.taskEnded(execution); f.build.taskEnded(execution); }
    f.build.ended(execution, 0); f.build.ended(execution, 0);
    if (!taskFirst) { f.build.taskEnded(execution); f.build.taskEnded(execution); }
    f.resolve(execution); await tick();
    assert.equal(f.reloads(), 1); assert.deepEqual(f.reports, ['Built Target']); assert.equal(f.run().kind, 'IDLE');
  }
});

for (const kind of ['BUILD', 'CONFIGURE'] as const) {
  test(`${kind} shares exclusion in STARTING and RUNNING and retains one operation`, async () => {
    const f = fixture(), other = kind === 'BUILD' ? 'CONFIGURE' : 'BUILD', execution = {};
    f.build.start(kind); const operation = f.run().operation;
    f.trust(false); f.build.start(other); assert.equal(f.calls(), 1);
    assert.equal(f.reports.at(-1), 'Build already active');
    f.resolve(execution); await tick(); assert.equal(f.run().operation, operation);
    f.build.start(other); assert.equal(f.calls(), 1); assert.equal(f.run().operation, operation);
    f.build.ended({}, 0); assert.equal(f.run().kind, 'RUNNING');
    f.build.ended(execution, 0); assert.equal(f.run().kind, 'IDLE');
    assert.equal(f.reloads(), kind === 'BUILD' ? 1 : 0);
    assert.equal(f.reports.at(-1), kind === 'BUILD' ? 'Built Target' : 'Configured');
  });
  for (const outcome of ['process', 'task', 'both', 'reject', 'dispose']) {
    test(`${kind} early ${outcome} reconciles identity and settles once`, async () => {
      const f = fixture(), execution = {}; f.build.start(kind);
      f.build.ended({}, 0); f.build.taskEnded({});
      if (outcome !== 'process') f.build.taskEnded(execution);
      if (outcome !== 'task') f.build.ended(execution, 0);
      if (outcome === 'dispose') f.build.dispose();
      if (outcome === 'reject') f.reject(); else f.resolve(execution);
      await tick();
      f.build.ended(execution, 0); f.build.taskEnded(execution);
      assert.equal(f.reloads(), kind === 'BUILD' && ['process', 'both'].includes(outcome) ? 1 : 0);
      assert.equal(f.reports.length, outcome === 'dispose' ? 0 : 1);
      if (['process', 'both'].includes(outcome)) assert.equal(f.reports[0], kind === 'BUILD' ? 'Built Target' : 'Configured');
      assert.equal(f.run().kind, outcome === 'dispose' ? 'DISPOSED' : 'IDLE');
    });
  }
}

for (const kind of ['BUILD', 'CONFIGURE'] as const) for (const end of ['failure', 'undefined', 'task', 'dispose']) {
  test(`${kind} RUNNING ${end} never reloads and duplicate ends are inert`, async () => {
    const f = fixture(), execution = {}; f.build.start(kind); f.resolve(execution); await tick();
    if (end === 'dispose') f.build.dispose();
    else if (end === 'task') f.build.taskEnded(execution);
    else f.build.ended(execution, end === 'failure' ? 7 : undefined);
    f.build.ended(execution, 0); f.build.taskEnded(execution);
    assert.equal(f.reloads(), 0); assert.equal(f.reports.length, end === 'dispose' ? 0 : 1);
    assert.equal(f.run().kind, end === 'dispose' ? 'DISPOSED' : 'IDLE');
  });
}
