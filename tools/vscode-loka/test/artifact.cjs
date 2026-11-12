// Deliberately outside test/*.test.ts and tsconfig's TypeScript-only include.
const assert = require('node:assert/strict');
const Module = require('node:module');
const {resolve} = require('node:path');
const {existsSync} = require('node:fs');
const manifest = require('../package.json');
const entry = resolve(__dirname, '..', manifest.main);
assert.ok(existsSync(entry), 'vscode-loka artifact: missing main output; run npm run build');
const {fakeHost} = require('../out/test/fake.js');
const f = fakeHost();
const original = Module._load;
let intercepted = 0;
try {
  Module._load = function(request, parent, isMain) {
    if (request === 'vscode') { ++intercepted; return f.api; }
    return original.call(this, request, parent, isMain);
  };
  assert.equal(require('vscode'), f.api, 'positive control: vscode interception');
  assert.equal(require('node:fs'), original('node:fs', module, false), 'other requests use original loader');
  const before = intercepted;
  const extension = require(entry);
  assert.equal(typeof extension.activate, 'function');
  assert.equal(typeof extension.deactivate, 'function');
  assert.ok(intercepted > before, 'entry must import vscode through the redirected loader');
  extension.activate(f.context);
  assert.deepEqual([...f.commands.keys()], ['loka.preview.open', 'loka.preview.build', 'loka.preview.reload', 'loka.preview.configure', 'loka.preview.selectExample']);
  assert.equal(f.subscriptions.length, 9, 'UI, commands, and both task listeners registered');
  assert.equal(f.context.subscriptions.length, 1, 'activation disposal registered');
  f.command('open');
  assert.equal(f.subscriptions.length, 11, 'panel listeners registered');
  extension.deactivate();
  assert.ok(f.subscriptions.every(s => s.attempts === 1 && !s.active), 'deactivate must release every subscription');
  assert.equal(f.panels[0].disposeAttempts, 1);
  f.context.subscriptions[0].dispose();
  assert.ok(f.subscriptions.every(s => s.attempts === 1), 'context disposal is idempotent');
} finally {
  Module._load = original;
  f.cleanup();
}
assert.equal(Module._load, original, 'positive control: loader restored');
console.log('vscode-loka artifact: passed (CommonJS entry, activation, commands, listeners, disposal, loader restoration)');
