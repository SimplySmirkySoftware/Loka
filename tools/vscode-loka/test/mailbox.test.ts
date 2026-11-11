import test from 'node:test';
import assert from 'node:assert/strict';
import {createMailbox} from '../src/mailbox.ts';

test('head remains until handle returns: outer-before, outer-after, nested', () => {
  const trace: string[] = [];
  const box = createMailbox<string>(event => {
    if (event === 'A') { trace.push('outer-before'); box.post('B'); trace.push('outer-after'); }
    else trace.push('nested');
  }, () => assert.fail('unexpected fault'));
  box.post('A');
  assert.deepEqual(trace, ['outer-before', 'outer-after', 'nested']);
  box.post('C'); assert.equal(trace.length, 4);
});
for (const original of [new Error('primary'), undefined]) {
  test(`A queues B, partially writes and throws ${String(original)}; cleanup reentry and throw stay inert`, () => {
    const trace: string[] = []; let cleanups = 0;
    const box = createMailbox<string>(event => {
      if (event === 'A') {
        box.post('B'); trace.push('partial'); throw original;
      }
      trace.push(event);
    }, () => { ++cleanups; box.post('cleanup'); throw new Error('cleanup'); });
    let caught: unknown = 'not thrown';
    try { box.post('A'); } catch (error) { caught = error; }
    assert.equal(caught, original, 'original thrown value');
    box.post('later');
    assert.deepEqual(trace, ['partial'], 'pending, cleanup and later events must be discarded');
    assert.equal(cleanups, 1);
  });
}
