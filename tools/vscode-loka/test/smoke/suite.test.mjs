import test from 'node:test';
import assert from 'node:assert/strict';
import {setImmediate} from 'node:timers/promises';
import {grantTrust} from './suite.cjs';

for (const mode of ['late grant', 'no grant', 'early grant', 'command failure', 'subscription failure', 'ready failure']) {
  test('suite trust: ' + mode, async t => {
    t.mock.timers.enable({apis: ['setTimeout', 'Date'], now: 0});
    let grant, prepare, disposed = 0, readyAt, settled = false;
    const preparation = new Promise(resolve => { prepare = resolve; });
    const vscode = {
      workspace: {onDidGrantWorkspaceTrust(callback) {
        if (mode === 'subscription failure') throw new Error(mode);
        grant = callback;
        return {dispose() { disposed++; }};
      }},
      commands: {async executeCommand(command) {
        assert.equal(command, 'workbench.trust.manage');
        if (mode !== 'subscription failure') assert.equal(typeof grant, 'function');
        await preparation;
        if (mode === 'command failure') throw new Error(mode);
      }}
    };
    const timers = new Set();
    const clock = {
      setTimeout(callback, ms) { const timer = setTimeout(callback, ms); timers.add(timer); return timer; },
      clearTimeout(timer) { clearTimeout(timer); timers.delete(timer); }
    };
    const outcome = grantTrust(vscode, async () => {
      if (mode === 'ready failure') throw new Error(mode);
      readyAt = Date.now();
    }, clock).then(() => { settled = true; }, error => { settled = true; return error; });
    if (mode === 'early grant') grant();
    t.mock.timers.tick(30000);
    await setImmediate();
    assert.equal(settled, false);
    assert.equal(readyAt, undefined);
    prepare();
    await setImmediate();
    if (mode.endsWith('failure')) {
      assert.match((await outcome).message, new RegExp(mode));
    } else {
      assert.equal(readyAt, 30000);
      t.mock.timers.tick(19999);
      await setImmediate();
      if (mode !== 'early grant') assert.equal(settled, false);
      if (mode === 'late grant') grant();
      if (mode === 'no grant') t.mock.timers.tick(1);
      const error = await outcome;
      if (mode === 'no grant') assert.match(error.message, /Trust grant timed out/);
      else assert.equal(error, undefined);
    }
    assert.equal(timers.size, 0);
    assert.equal(disposed, mode === 'subscription failure' ? 0 : 1);
    t.mock.timers.tick(20000);
    await setImmediate(); // A cancelled timer must not create a later unhandled rejection.
  });
}

const {Lifetime, TaskObserver} = await import('./suite.cjs');
function tasksFixture(life) {
  const handlers = {};
  class ShellExecution { constructor(commandLine, options) { Object.assign(this, {commandLine, options}); } }
  const folder = {uri: {fsPath: '/fixture'}};
  const execution = {task: {name: 'Configure', source: 'Loka', definition: {type: 'loka'}, scope: folder,
    execution: new ShellExecution('exit 0', {cwd: '/fixture'})}};
  const vscode = {ShellExecution, tasks: {taskExecutions: []}, commands: {executeCommand() {}}};
  for (const key of ['onDidStartTask', 'onDidStartTaskProcess', 'onDidEndTaskProcess'])
    vscode.tasks[key] = callback => { handlers[key] = callback; return {dispose() { delete handlers[key]; }}; };
  const emit = (key, value = execution) => handlers[key]({execution: value, exitCode: 0, processId: 42});
  const observer = new TaskObserver(vscode, life);
  return {vscode, execution, folder, emit, observer, handlers};
}
for (const mode of ['early events', 'wrong name', 'wrong source', 'wrong type', 'wrong scope', 'wrong shell',
  'wrong command', 'wrong cwd', 'two starts', 'foreign process', 'foreign end', 'preexisting']) {
  test('task identity: ' + mode, async t => {
    t.mock.timers.enable({apis: ['setTimeout', 'Date'], now: 0});
    const life = new Lifetime(), f = tasksFixture(life);
    if (mode === 'wrong name') f.execution.task.name = 'Build';
    if (mode === 'wrong source') f.execution.task.source = 'Other';
    if (mode === 'wrong type') f.execution.task.definition.type = 'other';
    if (mode === 'wrong scope') f.execution.task.scope = {...f.folder};
    if (mode === 'wrong shell') f.execution.task.execution = {...f.execution.task.execution};
    if (mode === 'wrong command') f.execution.task.execution.commandLine = 'exit 3';
    if (mode === 'wrong cwd') f.execution.task.execution.options.cwd = '/other';
    if (mode === 'preexisting') f.vscode.tasks.taskExecutions.push(f.execution);
    f.vscode.commands.executeCommand = () => {
      f.emit('onDidStartTask');
      if (mode === 'two starts') f.emit('onDidStartTask');
      f.emit('onDidStartTaskProcess', mode === 'foreign process' ? {...f.execution} : f.execution);
      f.emit('onDidEndTaskProcess', mode === 'foreign end' ? {...f.execution} : f.execution);
      return Promise.resolve(); // All events precede command settlement.
    };
    const pending = life.run(() => f.observer.configure(f.folder, 'exit 0', 0), async () => {}).catch(e => e);
    await setImmediate();
    t.mock.timers.tick(20500); await setImmediate();
    const outcome = await pending;
    if (mode === 'early events') assert.deepEqual(outcome, {starts: 1, exit: 0, processId: 42});
    else assert.ok(outcome instanceof Error, mode);
    assert.equal(Object.keys(f.handlers).length, 0);
  });
}
test('suite lifetime rejects the exported race while body awaits, late continuation is inert', async t => {
  t.mock.timers.enable({apis: ['setTimeout']});
  const life = new Lifetime();
  let resume, errors = 0, operations = 0;
  const pending = life.run(async () => {
    await new Promise(resolve => { resume = resolve; });
    await life.command({commands: {executeCommand() { operations++; }}}, 'late');
  }, async () => { errors++; }, 180000).catch(e => e);
  await setImmediate(); t.mock.timers.tick(180000); await setImmediate();
  assert.match((await pending).message, /Suite lifetime timed out/);
  resume(); await setImmediate();
  assert.equal(operations, 0); assert.equal(errors, 1); assert.equal(life.timers.size, 0);
});
test('negative task instrument detects a start even when executions is already empty', async () => {
  const life = new Lifetime(), f = tasksFixture(life);
  assert.deepEqual(f.observer.negative(), {starts: 0, executions: 0});
  f.emit('onDidStartTask');
  assert.throws(() => f.observer.negative(), /Unexpected task/);
  await life.run(async () => {}, async () => {});
});

test('suite trust budget starts at publication, not delayed go', async t => {
  t.mock.timers.enable({apis: ['setTimeout']});
  let disposed = false;
  const pending = grantTrust({workspace: {onDidGrantWorkspaceTrust() { return {dispose() { disposed = true; }}; }},
    commands: {async executeCommand() {}}}, start => { start(); return new Promise(() => {}); }).catch(e => e);
  await setImmediate(); t.mock.timers.tick(20000); await setImmediate();
  assert.match((await pending).message, /Trust grant timed out/); assert.equal(disposed, true);
});

for (const kind of ['T3', 'R']) test('suite operations and files: ' + kind, {timeout: 10000}, async () => {
  const {mkdtemp, readFile, writeFile, rm} = await import('node:fs/promises');
  const {runSuite} = await import('./suite.cjs');
  const directory = await mkdtemp('/tmp/loka-suite-test-');
  const life = new Lifetime(), f = tasksFixture(life);
  // runSuite creates its own observer using the same event ports.
  const calls = [], seen = [];
  let grant, selectionResolve, command = 'exit 3', tab = false;
  const vscode = {...f.vscode, ConfigurationTarget: {WorkspaceFolder: 3},
    extensions: {getExtension() { return {isActive: true, async activate() {}}; }},
    workspace: {isTrusted: false, workspaceFolders: [f.folder],
      onDidGrantWorkspaceTrust(cb) { grant = cb; return {dispose() {}}; },
      getConfiguration(section, uri) {
        if (section === 'security.workspace.trust') return {get() { return true; }};
        assert.equal(uri, f.folder.uri);
        return {get(key) { return key === 'buildDirectory' ? '/missing' : command; },
          async update(key, value, target) {
            assert.equal(key, 'configureCommand'); assert.equal(target, 3);
            await Promise.resolve(); command = value; calls.push('updated');
          }};
      }},
    window: {tabGroups: {get all() { return [{tabs: tab ? [{label: 'Loka Preview'}] : []}]; }}},
    commands: {async getCommands() { return []; }, executeCommand(name) {
      calls.push(name);
      if (name === 'loka.preview.open') tab = true;
      if (name === 'loka.preview.selectExample') return new Promise(resolve => { selectionResolve = resolve; });
      if (name === 'notifications.clearAll') selectionResolve();
      if (name === 'loka.preview.configure' && vscode.workspace.isTrusted) {
        f.execution.task.execution.commandLine = command;
        f.handlers.onDidStartTask({execution: f.execution});
        f.handlers.onDidStartTaskProcess({execution: f.execution, processId: 10});
        f.handlers.onDidEndTaskProcess({execution: f.execution, exitCode: command === 'exit 3' ? 3 : 0});
      }
      return Promise.resolve();
    }}
  };
  const pending = life.run(() => runSuite(vscode, directory, kind, life), async error => { throw error; });
  pending.catch(() => {});
  try {
    const total = 6;
    for (let n = 0; n < total; n++) {
      let value;
      for (const deadline = Date.now() + 5000; !value;) {
        try { value = JSON.parse(await readFile(`${directory}/step-${n}.json`, 'utf8')); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
        if (Date.now() >= deadline) throw new Error('step publication missing');
        if (!value) await new Promise(resolve => setTimeout(resolve, 10));
      }
      seen.push(value.cell + '/' + value.phase);
      if (value.phase === 'ready') { vscode.workspace.isTrusted = true; grant(); }
      if (value.cell === 'K1') {
        assert.equal(command, 'exit 0');
        assert.equal(value.facts.exit, 0);
        assert.ok(calls.indexOf('updated') < calls.lastIndexOf('loka.preview.configure'));
      }
      await writeFile(`${directory}/go-${n}`, '');
    }
    await pending;
    assert.deepEqual(seen, kind === 'T3'
      ? ['T1/ready', 'T1/trusted', 'K3/observed', 'K2/observed', 'K1/observed', 'T3/observed']
      : ['R1/observed', 'R3/observed', 'R2/observed', 'R4/observed', 'R5/toast', 'R5/cleared']);
    assert.equal(calls.filter(c => c === 'loka.preview.open').length, 1);
    if (kind === 'R') assert.ok(calls.indexOf('notifications.clearAll') > calls.indexOf('loka.preview.selectExample'));
  } finally { await rm(directory, {recursive: true, force: true}); }
});

for (const mode of ['good', 'process', 'args missing', 'args reordered', 'args extra', 'cwd', 'shell']) {
  test('Build uses the same task identity matcher: ' + mode, async t => {
    t.mock.timers.enable({apis: ['setTimeout', 'Date'], now: 0});
    const life = new Lifetime(), f = tasksFixture(life);
    class ProcessExecution { constructor() { this.process = 'cmake'; this.args = ['--build', '/build', '--target', 'LokaMineWeb']; this.options = {cwd: '/fixture'}; } }
    f.vscode.ProcessExecution = ProcessExecution;
    f.execution.task.name = 'Build';
    const execution = new ProcessExecution();
    if (mode === 'process') execution.process = 'other';
    if (mode === 'args missing') execution.args.pop();
    if (mode === 'args extra') execution.args.push('--clean-first');
    if (mode === 'args reordered') execution.args.reverse();
    if (mode === 'cwd') execution.options.cwd = '/other';
    f.execution.task.execution = mode === 'shell' ? new f.vscode.ShellExecution('cmake --build /build', {cwd: '/fixture'}) : execution;
    f.vscode.commands.executeCommand = name => {
      assert.equal(name, 'loka.preview.build');
      f.emit('onDidStartTask'); f.emit('onDidStartTaskProcess'); f.emit('onDidEndTaskProcess');
      return Promise.resolve();
    };
    const pending = life.run(() => f.observer.build(f.folder, '/build', 'LokaMineWeb'), async () => {}).catch(e => e);
    await setImmediate(); t.mock.timers.tick(500); await setImmediate();
    const value = await pending;
    if (mode === 'good') assert.equal(value.exit, 0);
    else assert.match(value.message, /identity mismatch/);
  });
}
