import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {signalOwnChild, RunOwner} from './run.mjs';
const child = () => Object.assign(new EventEmitter(), {pid: 314, exitCode: null, signalCode: null});
for (const origin of ['direct', 'exit', 'promise', 'nextTick']) test('signal door defers: ' + origin, async () => {
  const target = child(), calls = [];
  let pending;
  const request = () => { pending = signalOwnChild(target, 'SIGKILL', true,
    {version: '24.0.0', send: (...args) => calls.push(args)}); assert.equal(calls.length, 0); };
  if (origin === 'direct') request();
  if (origin === 'exit') { target.on('exit', request); target.emit('exit'); }
  if (origin === 'promise') await Promise.resolve().then(request);
  if (origin === 'nextTick') await new Promise(resolve => process.nextTick(() => { request(); resolve(); }));
  assert.equal(await pending, true);
  assert.deepEqual(calls, [[-314, 'SIGKILL']]);
});
for (const field of ['exitCode', 'signalCode']) test('signal door skips ended child: ' + field, async () => {
  const target = child(); target[field] = field === 'exitCode' ? 0 : 'SIGTERM';
  assert.equal(await signalOwnChild(target, 'SIGTERM', false,
    {version: '22.18.0', send() { assert.fail('ended child signalled'); }}), false);
});
test('signal door waits for the whole exit-notification batch', async () => {
  const a = child(), b = child();
  let pending;
  a.on('exit', () => { pending = signalOwnChild(b, 'SIGKILL', false,
    {version: '24.0.0', send() { assert.fail('reaped B signalled'); }}); });
  a.exitCode = 0; a.emit('exit'); b.exitCode = 0; b.emit('exit');
  assert.equal(await pending, false);
});
test('signal door refuses unaudited versions and absent PID', async () => {
  for (const version of ['22.19.0', '24.1.0', '25.0.0']) await assert.rejects(
    signalOwnChild(child(), 'SIGTERM', false, {version, send() { assert.fail(); }}), /Unverified Node/);
  assert.equal(await signalOwnChild({...child(), pid: undefined}, 'SIGTERM', false,
    {version: '24.0.0', send() { assert.fail(); }}), false);
});
test('early X death and repeated cleanup retain one disposal', async () => {
  const owner = new RunOwner('/tmp/loka-smoke-nonexistent-signal-test', {signal: (c, sig, group) =>
    signalOwnChild(c, sig, group, {version: '24.0.0', send() { assert.fail('ended X signalled'); }})});
  owner.x = {child: {...child(), exitCode: 1}, outcome: {code: 1}, closed: Promise.resolve({code: 1})};
  assert.throws(() => owner.check(), /Xvfb exited early/);
  await Promise.all([owner.cleanup(), owner.cleanup()]);
  assert.equal(owner.timers.size, 0);
});
