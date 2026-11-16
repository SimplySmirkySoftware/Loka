import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {checkWorkspaceFacts, workspaceArgs, recommendationExpression, cancelInstallExpression,
  normalStatus, markedStatus} from './workspace.mjs';

test('workspace launches isolate trust and use an ordinary installed driver', () => {
  for (const shared of [undefined, '/cell/fresh-shared']) {
    const args = workspaceArgs('/cell', '/cell/ws', shared);
    assert.ok(args.includes('--shared-data-dir=' + (shared || '/cell/shared')));
    assert.ok(args.includes('--extensions-dir=/cell/ext'));
    assert.ok(args.includes('--user-data-dir=/cell/user'));
    assert.equal(args.at(-1), '/cell/ws');
    assert.ok(!args.some(arg => arg.includes('extensionDevelopmentPath') || arg.includes('extensionTestsPath')));
  }
  assert.notEqual(markedStatus, normalStatus);
});
test('workspace assertions discriminate activation, exact placement, trust and absence', () => {
  const workspace = '/cell/ws';
  const facts = {trusted: true, defined: true, active: true,
    folders: [workspace], extensionPath: workspace + '/.vscode/extensions/loka-preview'};
  const expected = {trusted: true, defined: true, active: true};
  checkWorkspaceFacts(facts, workspace, expected);
  for (const patch of [{trusted: false}, {defined: false}, {active: false},
    {extensionPath: '/source/extension'}, {folders: []}]) {
    assert.throws(() => checkWorkspaceFacts({...facts, ...patch}, workspace, expected));
  }
  checkWorkspaceFacts({...facts, trusted: false, defined: false, active: false, extensionPath: null}, workspace,
    {trusted: false, defined: false});
});
test('recommendation row must belong to the Workspace Recommendations pane', () => {
  const action = {innerText: 'Install Workspace Extension', classList: {contains: () => false}};
  const row = {textContent: 'Loka Preview', querySelectorAll: () => [action]};
  let title = 'Other Recommendations', rows = [row];
  const pane = {querySelector: () => ({textContent: title}), querySelectorAll: () => rows};
  const document = {querySelectorAll: () => [pane]};
  assert.equal(vm.runInNewContext(recommendationExpression, {document}), false);
  title = 'Workspace Recommendations'; rows = [];
  assert.equal(vm.runInNewContext(recommendationExpression, {document}), false);
  rows = [row]; assert.equal(vm.runInNewContext(recommendationExpression, {document}), true);
  row.offsetParent = null; assert.equal(vm.runInNewContext(recommendationExpression, {document}), false);
  delete row.offsetParent; action.offsetParent = null;
  assert.equal(vm.runInNewContext(recommendationExpression, {document}), false);
});
test('restricted installer cancels only the matching trust modal', () => {
  let clicked = 0, textContent = 'Unrelated action';
  const cancel = {textContent: 'Cancel', click: () => clicked++};
  const dialog = {querySelectorAll: () => [{textContent}, cancel]};
  const document = {querySelectorAll: () => [dialog]};
  assert.equal(vm.runInNewContext(cancelInstallExpression, {document}), false);
  assert.equal(clicked, 0);
  textContent = 'Trust Workspace & Install';
  assert.equal(vm.runInNewContext(cancelInstallExpression, {document}), true);
  assert.equal(clicked, 1);
});
test('ordinary driver acknowledges non-awaited install and reports settlement after Cancel', async () => {
  const source = await readFile(new URL('./workspace-driver.cjs', import.meta.url), 'utf8');
  const files = new Map(), subscriptions = [];
  let tick, cancel, disposed = false;
  const fs = {readdirSync: () => [...files.keys()].map(key => key.slice('/box/'.length)),
    existsSync: file => files.has(file), readFileSync: file => files.get(file),
    writeFileSync: (file, value) => files.set(file, value),
    renameSync: (from, to) => { files.set(to, files.get(from)); files.delete(from); }};
  const vscode = {commands: {executeCommand: () => new Promise((_, reject) => { cancel = reject; })}};
  const context = {exports: {}, process: {env: {LOKA_WORKSPACE_MAILBOX: '/box'}},
    require: id => id === 'vscode' ? vscode : id === 'node:fs' ? fs : {join: (...args) => args.join('/')},
    setInterval: callback => { tick = callback; return 17; },
    clearInterval: timer => { assert.equal(timer, 17); disposed = true; }};
  vm.runInNewContext(source, context); context.exports.activate({subscriptions});
  files.set('/box/cmd-0001.json', JSON.stringify({command: 'install', nowait: true}));
  await tick();
  assert.deepEqual(JSON.parse(files.get('/box/res-0001.json')), {ok: true, value: 'started'});
  assert.ok(!files.has('/box/settled-0001.json'));
  cancel(new Error('Canceled'));
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(JSON.parse(files.get('/box/settled-0001.json')), {ok: false, error: 'Canceled'});
  subscriptions[0].dispose(); assert.equal(disposed, true);
});
