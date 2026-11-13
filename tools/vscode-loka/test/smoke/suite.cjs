const fs = require('node:fs/promises');
const {join} = require('node:path');
const flow = require('./flow.cjs');

// One writer per file; rename publishes a completed value to the launcher.
async function publish(directory, name, value) {
  await fs.writeFile(join(directory, name + '.tmp'), JSON.stringify(value));
  await fs.rename(join(directory, name + '.tmp'), join(directory, name));
}

// The grant budget begins only after the launcher can act on ready.
exports.grantTrust = async function (vscode, ready, clock = globalThis, check = () => {}) {
  let subscription, timer, release;
  try {
    const granted = new Promise(resolve => {
      subscription = vscode.workspace.onDidGrantWorkspaceTrust(resolve);
    });
    release = clock.keep && subscription ? clock.keep(subscription) : () => subscription?.dispose();
    // Subscription setup can reject before command preparation finishes.
    granted.catch(() => {});
    check();
    await vscode.commands.executeCommand('workbench.trust.manage');
    check();
    let start;
    const timeout = new Promise((_, reject) => { start = () => {
      if (timer !== undefined) return;
      timer = clock.setTimeout(() => reject(new Error('Trust grant timed out')), 20000);
    }; });
    timeout.catch(() => {});
    const readiness = ready(start);
    await Promise.race([readiness, timeout]);
    start(); // Standalone adapters may publish synchronously without the callback.
    await Promise.race([granted, timeout]);
  } finally {
    clock.clearTimeout(timer);
    release?.();
  }
};

/** One lifetime owns timers, subscriptions and late-command rejection records.
 * Cost: one suite; each operation checks one terminal mark, cleanup walks own resources. */
class Lifetime {
  constructor() { this.terminal = false; this.timers = new Set(); this.subscriptions = new Set(); this.failure = undefined; }
  check() {
    if (this.terminal) throw new Error('Suite terminated');
    if (this.failure) throw this.failure;
  }
  keep(subscription) {
    this.check();
    this.subscriptions.add(subscription);
    return () => { if (this.subscriptions.delete(subscription)) subscription.dispose(); };
  }
  command(vscode, name) {
    this.check();
    const pending = Promise.resolve(vscode.commands.executeCommand(name));
    pending.catch(error => { this.failure ||= error; });
    return pending;
  }
  setTimeout(callback, ms) {
    this.check();
    const timer = setTimeout(() => { this.timers.delete(timer); callback(); }, ms);
    this.timers.add(timer);
    return timer;
  }
  clearTimeout(timer) { clearTimeout(timer); this.timers.delete(timer); }
  pause(ms) { this.check(); return new Promise(resolve => this.setTimeout(resolve, ms)); }
  async until(read, ms, label) {
    const deadline = Date.now() + ms;
    for (;;) {
      this.check();
      const value = read();
      if (value) return value;
      if (Date.now() >= deadline) throw new Error(label + ' timed out');
      await this.pause(20);
    }
  }
  async limit(promise, ms, label) {
    let timer;
    try {
      return await Promise.race([promise, new Promise((_, reject) => {
        timer = this.setTimeout(() => reject(new Error(label + ' timed out')), ms);
      })]);
    } finally { this.clearTimeout(timer); }
  }
  async run(body, onError, ms = 180000) {
    try {
      try { return await this.limit(Promise.resolve().then(() => body(this)), ms, 'Suite lifetime'); }
      finally {
        this.terminal = true; // The sole terminal writer, before any error publication await.
        for (const timer of this.timers) clearTimeout(timer);
        this.timers.clear();
        for (const sub of this.subscriptions) sub.dispose();
        this.subscriptions.clear();
      }
    } catch (error) { await onError(error); throw error; }

  }
}
exports.Lifetime = Lifetime;

/** Subscribe once before admission; positive and negative windows share this instrument.
 * Cost: three subscriptions per suite, append one own event per callback; each window scans its rows. */
class TaskObserver {
  constructor(vscode, life) {
    this.vscode = vscode; this.life = life; this.events = [];
    for (const [method, kind] of [['onDidStartTask', 'start'], ['onDidStartTaskProcess', 'process'],
      ['onDidEndTaskProcess', 'end']]) {
      life.check();
      life.keep(vscode.tasks[method](event => this.events.push({kind, ...event})));
    }
  }
  idle() {
    this.life.check();
    if (this.vscode.tasks.taskExecutions.length !== 0) throw new Error('Tasks already executing');
  }
  begin() { this.idle(); return this.events.length; }
  negative(from = 0) {
    this.idle();
    if (this.events.slice(from).some(e => e.kind === 'start')) throw new Error('Unexpected task start');
    return {starts: 0, executions: 0};
  }
  /** Cost: one task operation; wrappers construct one expected identity for the shared observer. */
  configure(folder, command, exit, budget = 20000) {
    return this.perform(folder, {name: 'Configure', command}, exit, budget);
  }
  build(folder, directory, target) {
    return this.perform(folder, {name: 'Build', args: ['--build', directory, '--target', target]}, 0, 120000);
  }
  /** Cost: one command; bounded polling over this observer's events since admission. */
  async perform(folder, expected, exit, budget) {
    const from = this.begin();
    const pending = this.life.command(this.vscode, 'loka.preview.' + expected.name.toLowerCase());
    const rows = () => this.events.slice(from);
    const start = await this.life.until(() => rows().find(e => e.kind === 'start'), 10000, 'Task start');
    this.life.check();
    const task = start.execution.task;
    const execution = task.execution;
    const shape = expected.name === 'Configure'
      ? execution instanceof this.vscode.ShellExecution && execution.commandLine === expected.command
      : execution instanceof this.vscode.ProcessExecution && execution.process === 'cmake'
        && JSON.stringify(execution.args) === JSON.stringify(expected.args);
    if (task.name !== expected.name || task.source !== 'Loka' || task.definition.type !== 'loka'
      || task.scope !== folder || !shape || execution.options?.cwd !== folder.uri.fsPath)
      throw new Error('Task identity mismatch: expected ' + JSON.stringify(expected));
    const end = await this.life.until(() => rows().find(e => e.kind === 'end' && e.execution === start.execution),
      budget, 'Task end');
    await this.life.pause(500);
    this.life.check();
    if (rows().filter(e => e.kind === 'start').length !== 1) throw new Error('Expected exactly one task start');
    if (rows().some(e => e.execution !== start.execution)) throw new Error('Foreign task execution');
    if (end.exitCode !== exit) throw new Error('Task exit mismatch');
    await pending;
    this.life.check();
    return {starts: 1, exit: end.exitCode,
      processId: rows().find(e => e.kind === 'process')?.processId}; // Diagnostic only.
  }
}
exports.TaskObserver = TaskObserver;

/** Cost: each suite step writes one immutable snapshot then polls only its own acknowledgement. */
exports.runSuite = async function (vscode, directory, kind, life) {
  const observer = new TaskObserver(vscode, life);
  observer.idle();
  const result = {initialTrusted: vscode.workspace.isTrusted, trustGranted: false,
    trustEnabled: vscode.workspace.getConfiguration('security.workspace.trust').get('enabled')};
  life.check();
  const extension = vscode.extensions.getExtension('simplysmirkysoftware.loka-preview');
  if (!extension) throw new Error('Extension not found');
  life.check();
  await extension.activate();
  life.check();
  result.active = extension.isActive;
  result.commands = await vscode.commands.getCommands(true);
  life.check();
  const folder = vscode.workspace.workspaceFolders?.[0];
  result.folders = (vscode.workspace.workspaceFolders || []).map(f => f.uri.fsPath);
  const configuration = () => { life.check(); return vscode.workspace.getConfiguration('loka.preview', folder?.uri); };
  result.buildDirectory = configuration().get('buildDirectory');
  let sequence = 0;
  const tabs = () => { life.check(); return vscode.window.tabGroups.all.flatMap(group => group.tabs.map(tab => tab.label)); };
  const step = async (cell, phase = 'observed', facts = {}, published = () => {}) => {
    life.check();
    const n = sequence++;
    const value = {cell, phase, facts: {...result, finalTrusted: vscode.workspace.isTrusted, tabs: tabs(),
      ...(kind === 'R' ? observer.negative() : {}), ...(kind === 'F' ? {executions: vscode.tasks.taskExecutions.length} : {}), ...facts}};
    life.check();
    await fs.writeFile(join(directory, `step-${n}.json.tmp`), JSON.stringify(value));
    life.check();
    await fs.rename(join(directory, `step-${n}.json.tmp`), join(directory, `step-${n}.json`));
    life.check();
    published();
    for (;;) {
      life.check();
      try { await fs.access(join(directory, `go-${n}`)); break; }
      catch (error) { if (error.code !== 'ENOENT') throw error; }
      await life.pause(50);
    }
    life.check();
  };
  const open = async cell => {
    await life.command(vscode, 'loka.preview.open');
    await life.until(() => tabs().includes('Loka Preview'), 5000, 'Preview tab');
    await step(cell);
  };
  if (kind === 'R') {
    await step('R1');
    await life.command(vscode, 'loka.preview.configure');
    await step('R3');
    await open('R2');
    await life.command(vscode, 'loka.preview.build');
    await step('R4');
    let settled = false;
    const pending = life.command(vscode, 'loka.preview.selectExample');
    pending.then(() => { settled = true; }, () => { settled = true; });
    await step('R5', 'toast');
    life.check();
    if (settled) throw new Error('Select Example settled before clearAll');
    await life.command(vscode, 'notifications.clearAll');
    await life.limit(pending, 5000, 'Select Example clearAll');
    await step('R5', 'cleared');
    observer.negative();
  } else {
    const trustCell = kind === 'F' ? 'F1' : kind === 'T3' ? 'T1' : "T1'";
    await exports.grantTrust(vscode, published => step(trustCell, 'ready', {}, published), life, () => life.check());
    life.check();
    result.trustGranted = true;
    await step(trustCell, 'trusted');
    if (kind === 'F') {
      const rows = flow.skins(JSON.parse(await fs.readFile(join(directory, 'flow.json'), 'utf8')));
      const entry = async cell => { observer.idle(); await step(cell, 'entry'); };
      const select = async (cell, name) => {
        await entry(cell);
        await flow.picker(vscode, life, step, cell, rows.findIndex(r => r.name === name));
        await step(cell);
        await flow.picker(vscode, life, step, cell, 0, true);
      };
      await entry('F1');
      await step('F1', 'observed', await observer.configure(folder, configuration().get('configureCommand'), 0, 60000));
      const apps = flow.fixture(JSON.parse(await fs.readFile(join(result.buildDirectory, 'loka-web-apps.json'), 'utf8')));
      await entry('F2');
      await step('F2', 'observed', await observer.build(folder, result.buildDirectory, apps[0].target));
      await entry('F3');
      if (tabs().includes('Loka Preview')) throw new Error('F3 already has Preview tab');
      await open('F3');
      await select('F4', 'macos');
      await entry('F5');
      await flow.picker(vscode, life, step, 'F5', 1);
      await step('F5');
      await entry('F6');
      await step('F6', 'observed', await observer.build(folder, result.buildDirectory, apps[1].target));
      await select('F7', 'web');
      return;
    }
    if (kind === 'T3') {
      const from = observer.begin();
      await life.command(vscode, 'loka.preview.build');
      await life.pause(2000);
      await step('K3', 'observed', observer.negative(from));
      await step('K2', 'observed', await observer.configure(folder, 'exit 3', 3));
      life.check();
      await configuration().update('configureCommand', 'exit 0', vscode.ConfigurationTarget.WorkspaceFolder);
      life.check();
      if (configuration().get('configureCommand') !== 'exit 0') throw new Error('K1 effective setting did not update');
      await step('K1', 'observed', await observer.configure(folder, 'exit 0', 0));
    }
    await open(kind);
  }
};

exports.run = function () {
  const life = new Lifetime();
  return life.run(() => exports.runSuite(require('vscode'), process.env.LOKA_SMOKE_CELL,
    process.env.LOKA_SMOKE_KIND, life), error => publish(process.env.LOKA_SMOKE_CELL,
    'error.json', {error: String(error.stack || error)}), process.env.LOKA_SMOKE_KIND === 'F' ? 420000 : 180000);
};
