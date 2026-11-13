import test from 'node:test';
import assert from 'node:assert/strict';
import {runInNewContext} from 'node:vm';
import {readFile} from 'node:fs/promises';
import flow from './flow.cjs';
import {pickerExpression, pickerMatches, readWebview, webviewMatches, observe, webviewExpression} from './observe.mjs';
import {observeFlow, statusExpression} from './run.mjs';

const rows = flow.skins(JSON.parse(await readFile(new URL('../../../style-vocab.json', import.meta.url), 'utf8')));
const apps = [{id: 'LokaHelloWeb', name: 'HelloWorld', target: 'LokaHelloWeb'},
  {id: 'LokaMineWeb', name: 'MineSweeper', target: 'LokaMineWeb'}];
const owner = () => ({check() {}, async wait(read, _deadline, label) {
  for (let i = 0; i < 3; i++) if (await read()) return;
  throw new Error(label + ' timed out');
}});
const ui = (labels, focus = 0) => ({visible: 1, input: true, labels, focused: [focus]});
const labels = ['web, Requested', 'win32', 'macos', 'classic'];

test('fixture and skin facts refuse changed premises', () => {
  assert.equal(flow.fixture({version: 1, apps}), apps);
  for (const bad of [{}, {version: 1, apps: []}, {version: 1, apps: [...apps].reverse()}])
    assert.throws(() => flow.fixture(bad), /Fixture premise changed/);
  assert.throws(() => flow.skins({webSkins: rows.map(r => ({...r, spacing: [1, 2, 3]}))}), /must differ/);
  assert.throws(() => flow.skins({webSkins: [rows[0]]}), /must differ/);
});

for (const mode of ['pass', 'already open', 'wrong labels', 'duplicate widgets', 'no input', 'two focused',
  'no focus', 'stuck focus', 'wrong target', 'not closed', 'command rejection', 'unsettled']) {
  test('shared picker with fake CDP: ' + mode, async () => {
    let current = {visible: mode === 'already open' ? 1 : 0}, settle;
    const calls = [], phases = [];
    const life = {command(vscode, name) { const p = Promise.resolve(vscode.commands.executeCommand(name)); p.catch(() => {}); return p; },
      async limit(pending) { if (mode === 'unsettled') throw new Error('Picker command settlement timed out'); return pending; }};
    const vscode = {commands: {executeCommand(name) {
      calls.push(name);
      if (name === 'loka.preview.selectSkin') {
        current = ui(labels);
        if (mode === 'wrong labels') current.labels = ['other'];
        if (mode === 'duplicate widgets') current.visible = 2;
        if (mode === 'no input') current.input = false;
        if (mode === 'two focused') current.focused = [0, 1];
        if (mode === 'no focus') current.focused = [];
        return new Promise((resolve, reject) => { settle = mode === 'command rejection' ? () => reject(new Error(mode)) : resolve; });
      }
      if (name.endsWith('quickOpenSelectNext') && mode !== 'stuck focus') current.focused = [current.focused[0] + 1];
      if (mode === 'wrong target' && current.focused?.[0] === 2) current.focused = [3];
      if (name.endsWith('acceptSelectedQuickOpenItem')) {
        assert.deepEqual(current.focused, [2]);
        if (mode !== 'not closed') current = {visible: 0};
        if (mode !== 'unsettled') settle();
      }
    }}};
    const cdp = {async evaluate(expression) { assert.equal(expression, pickerExpression); return current; }};
    const step = async (_cell, phase) => {
      phases.push(phase);
      const expected = phase.startsWith('closed-') ? {closed: true} : {labels, focus: phase === 'open' ? 0 : Number(phase.split('-')[1])};
      await observe(owner(), () => cdp.evaluate(pickerExpression), v => pickerMatches(v, expected), expected, 10, 'F4/' + phase);
    };
    const pending = flow.picker(vscode, life, step, 'F4', 2);
    if (mode === 'pass') {
      await pending;
      assert.deepEqual(phases, ['closed-before', 'open', 'next-1', 'next-2', 'closed-after']);
      assert.equal(calls.filter(c => c.endsWith('quickOpenSelectNext')).length, 2);
    } else {
      await assert.rejects(pending, /timed out|command rejection/);
      if (!['not closed', 'command rejection', 'unsettled'].includes(mode))
        assert.ok(!calls.some(c => c.endsWith('acceptSelectedQuickOpenItem')));
    }
  });
}

const body = (offset = 9) => ({text: 'Loka Sample', scripts: 3, total: 1, rows: [
  {tag: 'span', text: 'Loka Sample', leaf: true, x: offset, y: offset, width: 50, height: 10, visible: true}]});
const expected = {kind: 'module', status: 'Loka: Running', id: apps[0].id, offset: 9};
for (const mode of ['good', 'no target', 'multiple', 'no body', 'open throws', 'evaluate throws', 'cancel', 'target changes']) {
  test('webview fake CDP: ' + mode, async () => {
    let polls = 0, closes = 0, cancelled = false;
    const own = owner(); own.check = () => { if (cancelled) throw new Error('interrupted'); };
    const targets = async () => {
      polls++;
      const target = {type: 'iframe', url: 'vscode-webview://fixture', id: polls};
      return mode === 'no target' ? [] : [target, ...(mode === 'multiple' ? [target] : []),
        {type: 'service_worker', url: 'vscode-webview://ignored'}];
    };
    const ids = [];
    const connection = target => ({async open() {
      ids.push(target.id);
      if (mode === 'open throws') throw new Error('open failed');
      if (mode === 'cancel') cancelled = true;
    }, async evaluate(expression) {
      assert.equal(expression, webviewExpression);
      if (mode === 'evaluate throws') throw new Error('frame detached');
      return mode === 'no body' ? null : body();
    }, close() { closes++; }});
    if (mode === 'multiple' || mode === 'cancel') await assert.rejects(readWebview(own, targets, connection), /exactly one|interrupted/);
    else {
      const value = await readWebview(own, targets, connection);
      assert.equal(webviewMatches(value, expected.status, expected), ['good', 'target changes'].includes(mode));
      if (mode === 'target changes') {
        await readWebview(own, targets, connection);
        assert.deepEqual(ids, [1, 2]);
      }
    }
    assert.equal(closes, ['multiple', 'no target'].includes(mode) ? 0 : mode === 'target changes' ? 2 : 1);
  });
}
test('module rejects missing, duplicate, hidden, zero size, stale offset and wrong status', () => {
  for (const change of [b => b.rows.pop(), b => b.rows.push(b.rows[0]),
    b => b.rows[0].visible = false, b => b.rows[0].width = 0,
    b => b.rows[0].height = 0, b => b.rows[0].x = 16]) {
    const value = body(); change(value);
    assert.equal(webviewMatches(value, expected.status, expected), false);
  }
  assert.equal(webviewMatches(body(), 'Loka: Waiting', expected), false);
  assert.equal(webviewMatches(body(10), expected.status, expected), true);
});
test('message needs readable body, exact unprefixed text, no scripts and no fixture markers', () => {
  const message = {kind: 'message', status: 'Loka: ' + flow.message, text: flow.message};
  const value = {text: flow.message, scripts: 0, total: 0, rows: []};
  assert.equal(webviewMatches(value, message.status, message), true);
  for (const bad of [null, {}, {...value, text: message.status}, {...value, scripts: 1},
    {...value, total: 1, rows: body().rows}]) assert.equal(webviewMatches(bad, message.status, message), false);
});
test('F4 and F7 reopened picker verifies Requested independently from focus', async () => {
  for (const cell of ['F4', 'F7']) {
    const requested = cell === 'F4' ? 'macos' : 'web';
    const labels = rows.map(r => r.name + (r.name === requested ? ', Requested' : ''));
    await observeFlow(owner(), {skins: rows}, {async evaluate() { return ui(labels); }}, cell, 'reopen-open', {}, apps, 10);
    await assert.rejects(observeFlow(owner(), {skins: rows}, {async evaluate() { return ui(['wrong']); }},
      cell, 'reopen-open', {}, apps, 10), /expected.*last read/);
  }
});
test('F plan budgets and reopen checks', () => {
  const plan = flow.plan(rows);
  for (const [cell, budget] of [['F1', 75000], ['F2', 135000], ['F6', 135000]])
    assert.equal(plan.find(([c, phase]) => c === cell && phase === 'observed')[2], budget);
  for (const cell of ['F4', 'F7']) assert.ok(plan.some(([c, p]) => c === cell && p === 'reopen-closed-after'));
});

test('cancellation closes a local CDP connection while its open is still pending', async () => {
  const own = {...owner(), abort: new AbortController()};
  own.check = () => own.abort.signal.throwIfAborted();
  let rejectOpen, closed = 0;
  const pending = readWebview(own, async () => [{type: 'iframe', url: 'vscode-webview://one'}], () => ({
    open: () => new Promise((_, reject) => { rejectOpen = reject; }),
    close() { closed++; rejectOpen(new Error('connection closed')); }
  }));
  await new Promise(resolve => setImmediate(resolve));
  own.abort.abort(new Error('interrupted'));
  await assert.rejects(pending, /interrupted/);
  assert.equal(closed, 1);
});

test('webview expression reacquires active-frame and requires a body, including access exceptions', async () => {
  const {runInNewContext} = await import('node:vm');
  let current, reads = 0;
  const document = {querySelector(selector) { assert.equal(selector, 'iframe#active-frame'); reads++; return current; }};
  const read = () => runInNewContext(webviewExpression, {document});
  assert.equal(read(), null);
  current = {get contentDocument() { throw new Error('cross-origin transition'); }};
  assert.equal(read(), null);
  current = {contentDocument: {body: null}};
  assert.equal(read(), null);
  current = {contentDocument: {body: {innerText: flow.message, getBoundingClientRect: () => ({left: 5, top: 10}), querySelectorAll: () => []},
    querySelectorAll: () => []}};
  assert.equal(read().text, flow.message);
  current = undefined;
  assert.equal(read(), null);
  assert.equal(reads, 5);
});

test('F1–F7 suite and launcher share the existing file handshake end to end', {timeout: 15000}, async () => {
  const {mkdtemp, writeFile, rm} = await import('node:fs/promises');
  const {join} = await import('node:path');
  const {runSuite, Lifetime} = await import('./suite.cjs');
  const {handshake, trustExpression} = await import('./run.mjs');
  const directory = await mkdtemp('/tmp/loka-flow-test-');
  const manifest = JSON.parse(await readFile(new URL('../../package.json', import.meta.url), 'utf8'));
  const catalog = {version: 1, apps: apps.map(a => ({...a, js: a.id + '.js'}))};
  await writeFile(join(directory, 'flow.json'), JSON.stringify({webSkins: rows}));
  const folder = {uri: {fsPath: '/fixture/ws'}};
  const handlers = {}, life = new Lifetime();
  let grant, status = '', panel = false, selected = 0, skin = 'web', pickerUI = {visible: 0}, settle;
  const calls = [], acks = [];
  class ShellExecution { constructor() { this.commandLine = 'fixture configure'; this.options = {cwd: folder.uri.fsPath}; } }
  class ProcessExecution { constructor() { this.process = 'cmake'; this.args = ['--build', directory, '--target', apps[selected].target]; this.options = {cwd: folder.uri.fsPath}; } }
  const vscode = {ShellExecution, ProcessExecution,
    tasks: {taskExecutions: []},
    workspace: {isTrusted: false, workspaceFolders: [folder],
      onDidGrantWorkspaceTrust(cb) { grant = cb; return {dispose() {}}; },
      getConfiguration(section) { return {get(key) {
        return section === 'security.workspace.trust' ? true : key === 'buildDirectory' ? directory : 'fixture configure';
      }}; }},
    window: {tabGroups: {get all() { return [{tabs: panel ? [{label: 'Loka Preview'}] : []}]; }}},
    extensions: {getExtension() { return {isActive: true, async activate() {}}; }},
    commands: {async getCommands() { return manifest.contributes.commands.map(c => c.command); }, async executeCommand(name) {
      calls.push(name);
      if (name === 'loka.preview.configure' || name === 'loka.preview.build') {
        const configure = name.endsWith('configure');
        const execution = {task: {name: configure ? 'Configure' : 'Build', source: 'Loka', definition: {type: 'loka'},
          scope: folder, execution: configure ? new ShellExecution() : new ProcessExecution()}};
        handlers.onDidStartTask({execution}); handlers.onDidStartTaskProcess({execution, processId: 1});
        if (configure) await writeFile(join(directory, 'loka-web-apps.json'), JSON.stringify(catalog));
        status = configure ? 'Loka: Configured' : panel ? 'Loka: Running' : 'Loka: Built ' + apps[selected].target;
        handlers.onDidEndTaskProcess({execution, exitCode: 0});
      } else if (name === 'loka.preview.open') { panel = true; status = 'Loka: Running'; }
      else if (name === 'loka.preview.selectSkin' || name === 'loka.preview.selectExample') {
        const isSkin = name.endsWith('selectSkin');
        pickerUI = ui(isSkin ? rows.map(r => r.name + (r.name === skin ? ', Requested' : '')) : apps.map(a => `${a.name}, ${a.target}`));
        return new Promise(resolve => { settle = accept => {
          if (accept) {
            if (isSkin) skin = rows[pickerUI.focused[0]].name;
            else { selected = pickerUI.focused[0]; status = 'Loka: ' + flow.message; }
          }
          pickerUI = {visible: 0}; resolve();
        }; });
      } else if (name.endsWith('quickOpenSelectNext')) pickerUI.focused[0]++;
      else if (name.endsWith('acceptSelectedQuickOpenItem')) settle(true);
      else if (name.endsWith('closeQuickOpen')) settle(false);
    }}
  };
  for (const method of ['onDidStartTask', 'onDidStartTaskProcess', 'onDidEndTaskProcess'])
    vscode.tasks[method] = callback => { handlers[method] = callback; return {dispose() { delete handlers[method]; }}; };
  const own = {abort: new AbortController(), cellDirectory: directory,
    check() { this.abort.signal.throwIfAborted(); }, limit: async p => p,
    async wait(read, deadline, label) {
      for (;;) { this.check(); const value = await read(); if (value) return value;
        if (Date.now() > deadline) throw new Error(label + ' timed out');
        await new Promise(resolve => setTimeout(resolve, 5)); }
    }};
  const cdp = {async evaluate(expression) {
    if (expression === trustExpression) { vscode.workspace.isTrusted = true; grant(); return true; }
    if (expression === statusExpression) return status;
    assert.equal(expression, pickerExpression); return pickerUI;
  }, async webview() {
    if (status === 'Loka: ' + flow.message) return {text: flow.message, scripts: 0, total: 0, rows: []};
    return {total: 1, rows: [{tag: selected ? 'button' : 'span', text: flow.markers[apps[selected].id].text, leaf: true,
      x: rows.find(r => r.name === skin).spacing[2],
      y: rows.find(r => r.name === skin).spacing[2], width: 10, height: 10, visible: true}]};
  }};
  const suite = life.run(() => runSuite(vscode, directory, 'F', life), async error => {
    await writeFile(join(directory, 'error.json'), JSON.stringify({error: error.message}));
  });
  suite.catch(() => {});
  own.code = {closed: suite.then(() => ({code: 0}))};
  try {
    await handshake(own, {kind: 'F', workspace: folder.uri.fsPath, buildDirectory: directory, skins: rows, started: Date.now()}, {
      async read(name) { try { return await readFile(join(directory, name), 'utf8'); } catch (e) { if (e.code === 'ENOENT') return undefined; throw e; } },
      async go(n) { acks.push(n); await writeFile(join(directory, 'go-' + n), ''); }
    }, async () => cdp);
    assert.equal(acks.length, flow.plan(rows).length);
    assert.equal(calls.filter(c => c === 'loka.preview.build').length, 2);
    assert.equal(calls.filter(c => c === 'loka.preview.selectSkin').length, 4);
    assert.equal(skin, 'web'); assert.equal(selected, 1);
  } finally { life.terminal = true; for (const timer of life.timers) clearTimeout(timer); await rm(directory, {recursive: true, force: true}); }
});

test('F entry refuses active tasks and an existing panel before Open', async () => {
  const cdp = {async evaluate() { return {visible: 0}; }};
  await assert.rejects(observeFlow(owner(), {}, cdp, 'F2', 'entry', {executions: 1}, apps, 10), /no running tasks/);
  let reads = 0;
  await assert.rejects(observeFlow(owner(), {}, {async evaluate() { return {visible: ++reads === 1 ? 1 : 0}; }},
    'F4', 'entry', {executions: 0}, apps, 10), /picker entry refused/);
  assert.equal(reads, 1); // An unrelated picker must fail admission, not be waited away.
  await assert.rejects(observeFlow(owner(), {}, cdp, 'F3', 'entry', {executions: 0, tabs: ['Loka Preview']}, apps, 10), /no Preview tab/);
});


test('CDP expressions are literal strings without interpolated values', async () => {
  for (const file of ['observe.mjs', 'run.mjs']) {
    const source = await readFile(new URL(file, import.meta.url), 'utf8');
    const expressions = [...source.matchAll(/export const \w+Expression = `([^`]+)`;/g)];
    assert.equal(expressions.length, file === 'observe.mjs' ? 2 : 3);
    for (const [, expression] of expressions) assert.ok(!expression.includes('${'));
  }
});

test('webview failure diagnostics retain only marker facts and 60 body characters', async () => {
  const value = body(16); value.text = 'a'.repeat(60) + 'PRIVATE_TAIL';
  const cdp = {async evaluate() { return 'Loka: Running'; }, async webview() { return value; }};
  await assert.rejects(observeFlow(owner(), {skins: rows}, cdp, 'F3', 'observed',
    {tabs: ['Loka Preview']}, apps, 10), error => {
    assert.match(error.message, /expected .*last read /);
    const last = JSON.parse(error.message.split('last read ')[1]);
    assert.deepEqual(last, {status: 'Loka: Running', markers: {
      LokaHelloWeb: [{id: 'LokaHelloWeb', x: 16, y: 16, visible: true}], LokaMineWeb: []},
      scripts: 3, text: 'a'.repeat(60)});
    assert.ok(!error.message.includes('PRIVATE_TAIL'));
    return true;
  });
});

test('generic page snapshot bounds rows and text and reports geometry without fixture matching', () => {
  const element = {tagName: 'BUTTON', innerText: '  ' + 'z'.repeat(300) + '  ', children: [{}],
    getBoundingClientRect: () => ({left: 14.4, top: 19.6, width: 50, height: 10}), checkVisibility: () => true};
  const d = {body: {innerText: 'b'.repeat(5000), getBoundingClientRect: () => ({left: 5, top: 10}),
    querySelectorAll(selector) { assert.equal(selector, '*'); return Array(2050).fill(element); }},
    defaultView: {getComputedStyle: () => ({display: 'block', visibility: 'visible'})},
    querySelectorAll(selector) { assert.equal(selector, 'script'); return [1, 2]; }};
  const read = () => runInNewContext(webviewExpression, {document: {querySelector: () => ({contentDocument: d})}});
  const snapshot = read();
  assert.equal(snapshot.rows.length, 2048);
  assert.equal(snapshot.total, 2050);
  assert.equal(snapshot.text.length, 4096);
  assert.equal(snapshot.scripts, 2);
  assert.deepEqual(JSON.parse(JSON.stringify(snapshot.rows[0])), {
    tag: 'button', text: 'z'.repeat(256), leaf: false, x: 9, y: 10, width: 50, height: 10, visible: true});
  for (const visibility of ['hidden', 'collapse']) {
    d.defaultView.getComputedStyle = () => ({display: 'block', visibility});
    assert.equal(read().rows[0].visible, false);
  }
});

test('launcher matches fixture text, selector and leaf requirement from generic rows', () => {
  for (const change of [b => b.rows[0].text = 'other', b => b.rows[0].leaf = false]) {
    const value = body(); change(value);
    assert.equal(webviewMatches(value, expected.status, expected), false);
  }
  const mine = {...expected, id: 'LokaMineWeb'};
  const value = body(); value.rows[0].text = 'New Game';
  assert.equal(webviewMatches(value, mine.status, mine), false);
  value.rows[0].tag = 'button'; value.rows[0].leaf = false;
  assert.equal(webviewMatches(value, mine.status, mine), true);
});

test('a capped snapshot is refused: a duplicate marker beyond the cap cannot pass as unique', () => {
  const row = text => ({tag: 'span', text, leaf: true, x: 16, y: 16, width: 10, height: 10, visible: true});
  const rows = [row('Loka Sample'), ...Array(2047).fill(row('filler'))];
  const expected = {kind: 'module', status: 'Loka: Running', id: 'LokaHelloWeb', offset: 16};
  assert.equal(webviewMatches({text: '', scripts: 1, total: 2048, rows}, 'Loka: Running', expected), true);
  // Same visible rows, but the page had more elements than the snapshot carries (e.g. a second marker at 2049).
  assert.equal(webviewMatches({text: '', scripts: 1, total: 2050, rows}, 'Loka: Running', expected), false);
  const message = {kind: 'message', status: 'Loka: m', text: 'm'};
  assert.equal(webviewMatches({text: 'm', scripts: 0, total: 1, rows: [row('x')]}, 'Loka: m', message), true);
  assert.equal(webviewMatches({text: 'm', scripts: 0, total: 3000, rows: Array(2048).fill(row('x'))}, 'Loka: m', message), false);
});
