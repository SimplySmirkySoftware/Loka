import test from 'node:test';
import assert from 'node:assert/strict';
import {createBuild} from '../src/build.ts';

function fixture() {
  let trusted = true, calls = 0, reloads = 0;
  let resolve!: (value: object) => void, reject!: (reason: unknown) => void;
  let throws = false;
  const reports: string[] = [];
  const build = createBuild({
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
  assert.equal(f.reloads(), 1); assert.equal(f.reports.length, 0); assert.equal(f.run().kind, 'IDLE');
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
