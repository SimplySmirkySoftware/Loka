import test from 'node:test';
import assert from 'node:assert/strict';
import {createContext, runInContext} from 'node:vm';
import {renderPreviewHtml} from '../src/previewHtml.ts';

function page() {
  const messages: {type: string; id: string; status?: number}[] = [];
  const listeners = new Map<string, (event: unknown) => void>();
  const context = createContext({acquireVsCodeApi: () => ({postMessage: (m: typeof messages[0]) => messages.push(m)}),
    addEventListener: (name: string, listener: (event: unknown) => void) => listeners.set(name, listener)});
  runInContext('window = globalThis', context);
  const html = renderPreviewHtml({kind: 'module', id: 'doc', nonce: 'nonce', cspSource: "'self'",
    exportName: 'Demo', jsUri: 'Demo.js', wasmUri: 'Demo.wasm'});
  const scripts = [...html.matchAll(/<script nonce="nonce">([\s\S]*?)<\/script>/g)].map(m => m[1]);
  runInContext(scripts[0], context);
  return {messages, context, boot: () => runInContext(scripts[1], context),
    dispose: () => listeners.get('message')!({data: {id: 'doc', type: 'dispose'}}),
    error: () => listeners.get('error')!({message: 'observer error'})};
}
const tick = async () => { for (let i = 0; i < 8; ++i) await Promise.resolve(); };

test('page acquisition failures acknowledge pending dispose once, never completed', async () => {
  for (const failure of ['missing', 'throw', 'reject']) {
    for (const pending of [false, true]) {
      const p = page();
      if (failure === 'throw') p.context.Demo = () => { throw new Error('factory'); };
      if (failure === 'reject') p.context.Demo = () => Promise.reject(new Error('factory'));
      if (pending) p.dispose();
      p.boot(); await tick();
      if (!pending) p.dispose();
      p.dispose();
      assert.equal(p.messages.filter(m => m.type === 'absent').length, 1);
      assert.equal(p.messages.filter(m => m.type === 'completed').length, 0);
      assert.ok(p.messages.every(m => m.id === 'doc'));
    }
  }
});
test('page completion is real, start once or zero with disposal pending', async () => {
  for (const pending of [false, true]) {
    const p = page(); let starts = 0, disposals = 0;
    let acquire!: (module: object) => void, complete!: () => void;
    const completed = new Promise<void>(yes => { complete = yes; });
    p.context.Demo = () => new Promise<object>(yes => { acquire = yes; });
    p.boot(); if (pending) p.dispose();
    acquire({web: {completed, start: () => { ++starts; return 0; }, dispose: () => { ++disposals; }}});
    await tick();
    assert.equal(starts, pending ? 0 : 1);
    if (!pending) p.dispose();
    assert.equal(disposals, 1); assert.equal(p.messages.filter(m => m.type === 'completed').length, 0);
    complete(); await tick(); p.dispose();
    assert.equal(p.messages.filter(m => m.type === 'completed').length, 1);
    assert.equal(disposals, 1);
  }
});
test('READY survives start throw and observer error', async () => {
  const p = page(); let disposals = 0, acquire!: (module: object) => void;
  p.context.Demo = () => new Promise<object>(yes => { acquire = yes; });
  p.boot(); p.error();
  acquire({web: {completed: new Promise(() => {}), start() { throw new Error('start'); },
    dispose() { ++disposals; }}});
  await tick(); p.error(); p.dispose();
  assert.equal(disposals, 1); assert.equal(p.messages.filter(m => m.type === 'absent').length, 0);
  assert.equal(p.messages.filter(m => m.type === 'error').length, 3);
});

test('PENDING observer error then disposal waits for acquisition and real completion', async () => {
  const p = page(); let starts = 0, disposals = 0;
  let acquire!: (module: object) => void, complete!: () => void;
  const completed = new Promise<void>(yes => { complete = yes; });
  p.context.Demo = () => new Promise<object>(yes => { acquire = yes; });
  p.boot(); p.error(); p.dispose();
  assert.equal(p.messages.filter(m => m.type === 'absent').length, 0);
  assert.equal(p.messages.filter(m => m.type === 'completed').length, 0);
  acquire({web: {completed, start() { ++starts; return 0; }, dispose() { ++disposals; }}});
  await tick();
  assert.equal(starts, 0); assert.equal(disposals, 1);
  assert.equal(p.messages.filter(m => m.type === 'absent').length, 0);
  assert.equal(p.messages.filter(m => m.type === 'completed').length, 0);
  complete(); await tick();
  assert.equal(p.messages.filter(m => m.type === 'completed').length, 1);
});
