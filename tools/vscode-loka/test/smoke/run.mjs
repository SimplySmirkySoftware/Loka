#!/usr/bin/env node
import {spawn, spawnSync} from 'node:child_process';
import {constants} from 'node:fs';
import {access, mkdir, mkdtemp, readFile, realpath, rm, stat, writeFile} from 'node:fs/promises';
import {createHash, randomBytes} from 'node:crypto';
import {dirname, isAbsolute, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export const repo = resolve(here, '../../../..');
const extension = resolve(here, '../..');
export const statuses = Object.freeze({R: 'Loka: Trust the workspace to preview',
  T3: 'Loka: Web catalog missing; run Configure', T2: 'Loka: Running'});
const cells = {R: 'R1 R3 R2 R4 R5', T3: 'T1 K3 K2 K1 T3', T2: "T1' T2"};
const allCells = "R1 R2 R3 R4 R5 T1 K3 K2 K1 T3 T1' T2";
const manifest = JSON.parse(await readFile(join(extension, 'package.json'), 'utf8'));
const commands = manifest.contributes.commands.map(item => item.command);
export const pin = JSON.parse(await readFile(join(here, 'vscode-pin.json'), 'utf8'));

/** Pure decision; malformed/missing records never become success. */
export function judge(cell, result, status, exit, {trusted = cell.kind !== 'R', panel = true} = {}) {
  const errors = [];
  if (!result || typeof result !== 'object' || Array.isArray(result)) return ['Missing or malformed step facts'];
  if (Object.hasOwn(result, 'error')) errors.push('Suite error: ' + result.error);
  if (result.active !== true || !Array.isArray(result.commands)
      || commands.length !== 6 || commands.some(id => !result.commands.includes(id))) errors.push('R1 activation/commands');
  if (result.trustEnabled !== true || result.initialTrusted !== false) errors.push('Initial Workspace Trust');
  if (result.finalTrusted !== trusted || result.trustGranted !== trusted) errors.push('Trust outcome');
  if (!Array.isArray(result.folders) || result.folders.length !== 1 || result.folders[0] !== cell.workspace) errors.push('Workspace folder');
  if (result.buildDirectory !== cell.buildDirectory) errors.push('Effective buildDirectory');
  if (panel && (!Array.isArray(result.tabs) || result.tabs.filter(label => label === 'Loka Preview').length !== 1)) errors.push('Preview tab');
  if (!Object.hasOwn(statuses, cell.kind) || status !== statuses[cell.kind]) errors.push('Status: ' + status);
  if (exit !== 0) errors.push('Exit: ' + exit);
  return errors;
}

/** Child output is hidden unless LOKA_SMOKE_VERBOSE=1; a failing run is rerun with it to see VS Code's log. */
const verbose = process.env.LOKA_SMOKE_VERBOSE === '1' ? 'inherit' : 'ignore';
export function prerequisiteDecision(facts, env = {}) {
  const required = env.LOKA_REQUIRE_VSCODE_SMOKE === '1' || env.LOKA_REQUIRE_VSCODE_RUNTIME === '1';
  const version = /^(?:v)?(\d+)\.(\d+)\.(\d+)$/.exec(facts.node || '');
  const supported = version && (+version[1] >= 24 || (+version[1] === 22 && +version[2] >= 18));
  const missing = [facts.platform !== 'linux' && 'Linux', !supported && 'Node >=24 or 22.18+',
    supported && !verifiedSignalVersions.includes(facts.node) &&
      `Node ${facts.node} is not in the audited list for child-signal ordering`,
    !facts.python && 'python3', !facts.xvfb && 'Xvfb', !facts.xauth && 'xauth', !env.LOKA_VSCODE && 'LOKA_VSCODE not set',
    !facts.artifact && 'out/extension.js not built'].filter(Boolean);
  if (facts.invalid) return {code: 1, lines: ['[fail] ' + allCells + ': ' + facts.invalid]};
  if (env.LOKA_REQUIRE_VSCODE_RUNTIME === '1' && !env.LOKA_WEB_BUILD) missing.push('LOKA_WEB_BUILD not set');
  if (missing.length) return {code: required ? 1 : 77,
    lines: [`[${required ? 'fail' : 'skip'}] ${allCells}: ${missing.join(', ')}`]};
  return {code: 0, lines: env.LOKA_WEB_BUILD ? [] : ["[skip] T1' T2: LOKA_WEB_BUILD not set"]};
}

async function optionalRead(path) {
  try { return await readFile(path, 'utf8'); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}
/** Probe only; stale locks are replaced by Xvfb, never removed by the launcher. */
export function pidAlive(pid) {
  try { process.kill(pid, 0); return true; }
  catch (error) { if (error.code === 'ESRCH') return false; if (error.code === 'EPERM') return true; throw error; }
}
async function readXLock(display) { return Number(await optionalRead(`/tmp/.X${display}-lock`)); }
export async function displayTaken(display, readLock = readXLock, alive = pidAlive) {
  const pid = await readLock(display);
  return Number.isInteger(pid) && pid > 0 && alive(pid);
}

export async function validateInputs(env) {
  let code;
  if (env.LOKA_VSCODE) {
    code = await realpath(env.LOKA_VSCODE);
    if (!(await stat(code)).isFile()) throw new Error('LOKA_VSCODE is not a file');
    await access(code, constants.X_OK);
    const product = JSON.parse(await readFile(join(dirname(code), 'resources/app/product.json'), 'utf8'));
    if (product.version !== pin.version || product.commit !== pin.commit) throw new Error('VS Code pin mismatch');
  }
  if (env.LOKA_WEB_BUILD && (!isAbsolute(env.LOKA_WEB_BUILD) || !(await stat(env.LOKA_WEB_BUILD)).isDirectory())) {
    throw new Error('LOKA_WEB_BUILD must be an absolute directory');
  }
  return code;
}

/** Source audit: Node 22.18.0 and 24.0.0 (REFUTE6), plus 24.19.0.
 * Exact releases only: this does not certify every patch or downstream build.
 * deps/uv/src/unix/{process,core,signal,linux}.c; src/{env,process_wrap}.cc;
 * lib/internal/child_process.js. wait_children drains notifications before run_check.
 * Cost: each cleanup request once; the captured spawned child only, no PID search. */
export const verifiedSignalVersions = Object.freeze(['22.18.0', '24.0.0', '24.19.0']);
export function signalOwnChild(child, signal, group = false,
  {version = process.versions.node, send = process.kill} = {}) {
  return new Promise((resolve, reject) => setImmediate(() => {
    try {
      if (!verifiedSignalVersions.includes(version)) throw new Error('Unverified Node signal ordering: ' + version);
      if (!Number.isInteger(child.pid) || child.pid <= 0) return resolve(false);
      if (child.exitCode !== null || child.signalCode !== null) return resolve(false);
      try { send(group ? -child.pid : child.pid, signal); }
      catch (error) { if (error.code !== 'ESRCH') throw error; }
      resolve(true);
    } catch (error) { reject(error); }
  }));
}

function childRecord(child) {
  const record = {child, outcome: undefined};
  record.closed = new Promise(resolve => {
    child.once('error', error => { record.error = error; });
    child.once('close', (code, signal) => { record.outcome = {code, signal}; resolve(record.outcome); });
  });
  return record;
}
/** Owns the run directory, X server, code group, CDP and every launcher timer.
 * Parallel to tests/web/driver/capture.mjs runBrowser: this owner also holds a file handshake. */
export class RunOwner {
  constructor(directory, {signal = signalOwnChild, readLock = readXLock} = {}) {
    this.directory = directory;
    this.signal = signal;
    this.readLock = readLock;
    this.timers = new Set();
    this.abort = new AbortController();
  }
  async limit(operation, ms, label) {
    let timer;
    try {
      return await Promise.race([operation, new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(label + ' timed out')), Math.max(1, ms));
        this.timers.add(timer);
      })]);
    } finally { clearTimeout(timer); this.timers.delete(timer); }
  }
  async pause() {
    let timer;
    await new Promise(resolve => { timer = setTimeout(resolve, 50); this.timers.add(timer); });
    this.timers.delete(timer);
  }
  check() {
    this.abort.signal.throwIfAborted();
    if (this.code?.error) throw this.code.error;
    if (this.code?.child && (this.code.child.exitCode !== null || this.code.child.signalCode !== null))
      throw new Error('VS Code exited early');
    if (this.code?.outcome) throw new Error('VS Code exited early: ' + JSON.stringify(this.code.outcome));
    if (this.x?.error) throw this.x.error;
    if (this.x?.child && (this.x.child.exitCode !== null || this.x.child.signalCode !== null))
      throw new Error('Xvfb exited early');
    if (this.x?.outcome) throw new Error('Xvfb exited early');
  }
  async wait(read, deadline, label, active = true) {
    for (;;) {
      if (active) this.check();
      if (Date.now() >= deadline) throw new Error(label + ' timed out');
      const value = await this.limit(Promise.resolve().then(read), deadline - Date.now(), label);
      if (active) this.check();
      if (value) return value;
      if (Date.now() >= deadline) throw new Error(label + ' timed out');
      await this.pause();
    }
  }
  async startX() {
    const deadline = Date.now() + 10000;
    const auth = join(this.directory, 'Xauthority');
    for (let display = 99; display < 119 && Date.now() < deadline; display++) {
      if (await displayTaken(display, this.readLock)) continue;
      const result = spawnSync('xauth', ['-f', auth, 'add', ':' + display, '.', randomBytes(16).toString('hex')],
        {timeout: Math.max(1, deadline - Date.now()), stdio: 'pipe'});
      if (result.error || result.status !== 0) throw new Error('xauth failed: ' + (result.error || result.stderr));
      this.x = childRecord(spawn('Xvfb', [':' + display, '-nolisten', 'tcp', '-auth', auth,
        '-screen', '0', '1280x800x24'], {stdio: verbose}));
      this.x.display = display;
      const acquired = await this.wait(async () => {
        this.abort.signal.throwIfAborted();
        if (this.x.error) throw this.x.error;
        const owner = await this.readLock(display);
        if (owner === this.x.child.pid && !this.x.outcome) return 'owned';
        if (this.x.outcome) {
          if (owner === this.x.child.pid) throw new Error('Xvfb exited leaving its own lock');
          return 'lost';
        }
        return false;
      }, deadline, 'X lock ownership', false);
      if (acquired === 'owned') return {DISPLAY: ':' + display, XAUTHORITY: auth};
      // The losing server has closed; another server's lock is never removed.
      this.x = undefined;
    }
    throw new Error('X display allocation failed (20 candidates / 10 seconds)');
  }
  startCode(executable, args, env) {
    this.code = childRecord(spawn(executable, args, {env, detached: true, stdio: verbose}));
    return Date.now();
  }
  async cleanup() {
    if (this.cleaning) return this.cleaning;
    this.cleaning = this.dispose();
    return this.cleaning;
  }
  async dispose() {
    const errors = [];
    const attempt = async action => { try { await action(); } catch (error) { errors.push(error.message); } };
    if (this.code) await attempt(async () => {
      await this.limit(this.signal(this.code.child, 'SIGKILL', true), 1000, 'Code signal');
      await this.limit(this.code.closed, 5000, 'Code close');
    });
    if (this.x) await attempt(async () => {
      await this.limit(this.signal(this.x.child, 'SIGTERM'), 1000, 'Xvfb signal');
      try { await this.limit(this.x.closed, 5000, 'Xvfb close'); }
      catch (error) {
        errors.push(error.message);
        await this.limit(this.signal(this.x.child, 'SIGKILL'), 1000, 'Xvfb escalation');
        await this.limit(this.x.closed, 5000, 'Xvfb killed close');
      }
      if (await this.readLock(this.x.display) === this.x.child.pid)
        throw new Error('Xvfb exited leaving its own lock');
    });
    await attempt(async () => this.cdp?.close());
    await attempt(() => this.limit(rm(this.directory, {recursive: true, force: true}), 5000, 'Run directory removal'));
    for (const timer of this.timers) clearTimeout(timer);
    this.timers.clear();
    if (errors.length) throw new Error('Cleanup failed: ' + errors.join('; '));
  }
}

// Workbench DOM expressions, including toastExpression below, are pin-dependent.
export const trustExpression = `(() => { const button = [...document.querySelectorAll('.workspace-trust-editor .monaco-button')].find(b => b.innerText.trim() === 'Trust'); if (!button) return false; button.click(); return true; })()`;
export const statusExpression = `document.getElementById('simplysmirkysoftware.loka-preview')?.innerText.trim() ?? ''`;

/** CDP remains local: the browser driver has an executable main and cannot be imported safely. */
async function connectCDP(owner, directory, deadline) {
  const port = await owner.wait(async () => {
    const contents = await optionalRead(join(directory, 'user/DevToolsActivePort'));
    if (contents === undefined) return false;
    const value = Number(contents.split('\n')[0]);
    if (!Number.isInteger(value) || value < 1 || value > 65535) throw new Error('Invalid DevToolsActivePort');
    return value;
  }, deadline, 'DevToolsActivePort');
  // The port file appears before the workbench page target does; poll until exactly one page exists.
  const page = await owner.wait(async () => {
    const request = new AbortController();
    try {
      const targets = await owner.limit((async () => {
        const response = await fetch(`http://127.0.0.1:${port}/json`,
          {signal: AbortSignal.any([owner.abort.signal, request.signal])});
        if (!response.ok) throw new Error('CDP target HTTP ' + response.status);
        return response.json();
      })(), 2000, 'CDP targets');
      const pages = targets.filter(target => target.type === 'page' && target.url.startsWith('vscode-file:'));
      if (pages.length > 1) throw new Error('Expected one workbench CDP page');
      return pages[0] || false;
    } finally { request.abort(); }
  }, deadline, 'workbench CDP page');
  const socket = new WebSocket(page.webSocketDebuggerUrl);
  // Attach the owner before awaiting open, so failed connections also close.
  owner.cdp = {close: () => socket.close()};
  await owner.limit(new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', () => reject(new Error('CDP connection error')), {once: true});
  }), deadline - Date.now(), 'CDP connection');
  let sequence = 0;
  return {
    async evaluate(expression) {
      const id = ++sequence;
      let message, closed;
      try {
        return await owner.limit(new Promise((resolve, reject) => {
          closed = () => reject(new Error('CDP closed'));
          message = event => {
            try {
              const reply = JSON.parse(event.data);
              if (reply.id !== id) return;
              if (reply.error || reply.result?.exceptionDetails) throw new Error('CDP evaluation failed: ' + JSON.stringify(reply));
              resolve(reply.result?.result?.value);
            } catch (error) { reject(error); }
          };
          socket.addEventListener('message', message);
          socket.addEventListener('close', closed);
          socket.addEventListener('error', closed);
          socket.send(JSON.stringify({id, method: 'Runtime.evaluate', params: {expression, returnByValue: true}}));
        }), 2000, 'CDP evaluation');
      } finally {
        socket.removeEventListener('message', message);
        socket.removeEventListener('close', closed);
        socket.removeEventListener('error', closed);
      }
    }
  };
}

/** One table owns order and next-step budgets. Facts remain suite-owned. */
export const steps = Object.freeze({
  T3: [['T1', 'ready'], ['T1', 'trusted'], ['K3', 'observed'], ['K2', 'observed', 45000],
    ['K1', 'observed', 45000], ['T3', 'observed']],
  R: [['R1', 'observed'], ['R3', 'observed'], ['R2', 'observed'], ['R4', 'observed'],
    ['R5', 'toast'], ['R5', 'cleared']],
  T2: [["T1'", 'ready'], ["T1'", 'trusted'], ['T2', 'observed']]
});
export const stepStatuses = Object.freeze({R3: 'Loka: Trust the workspace to build',
  R2: statuses.R, R4: 'Loka: Trust the workspace to build', K3: statuses.T3,
  K2: 'Loka: Build failed: 3', K1: 'Loka: Configured', T3: statuses.T3, T2: statuses.T2});
export const refusalText = 'Trust the workspace to select an example';
// The page only reports what it shows; the comparison with refusalText happens in the launcher, so no value is spliced into evaluated code.
export const toastExpression = `(() => ({text: [...document.querySelectorAll('.notifications-toasts .notification-list-item')].map(e => e.textContent).join('\\n'), picker: !!document.querySelector('.quick-input-widget')}))()`;

/** Cost: once per launch; one immutable step and one ack per table row, bounded CDP polls. */
export async function handshake(owner, cell, files, connect = connectCDP) {
  let deadline = cell.started + 40000;
  let cdp;
  let context = cell.kind + '/startup';
  const plan = steps[cell.kind];
  const errorCheck = async () => {
    const error = await files.read('error.json');
    if (error !== undefined) throw new Error('Suite error: ' + error);
  };
  try {
    for (let n = 0; n < plan.length; n++) {
      const [name, phase] = plan[n];
      context = `${name}/${phase}`;
      const raw = await owner.wait(async () => {
        await errorCheck();
        const value = await files.read(`step-${n}.json`);
        await errorCheck();
        return value;
      }, deadline, `step-${n}`);
      let step;
      try { step = JSON.parse(raw); } catch { throw new Error('Malformed step JSON'); }
      if (step.cell !== name || step.phase !== phase || !step.facts) throw new Error('Unexpected step identity');
      const facts = step.facts;
      // All original activation, trust, folder, setting and panel oracles remain.
      const invalid = judge(cell, facts, statuses[cell.kind], 0, {
        trusted: phase !== 'ready' && cell.kind !== 'R', panel: ['R2', 'T3', 'T2'].includes(name)
      });
      if (invalid.length) throw new Error(invalid.join('; '));
      if (['K1', 'K2'].includes(name) && (facts.starts !== 1 || facts.exit !== (name === 'K1' ? 0 : 3)))
        throw new Error('Task outcome');
      if ((cell.kind === 'R' || name === 'K3') && (facts.starts !== 0 || facts.executions !== 0))
        throw new Error('Unexpected task start');
      const observationDeadline = Date.now() + 10000;
      if (!cdp) cdp = await connect(owner, owner.cellDirectory, observationDeadline);
      if (phase === 'ready') await owner.wait(() => cdp.evaluate(trustExpression), observationDeadline, 'Trust button');
      if (stepStatuses[name]) {
        let last = '';
        try {
          await owner.wait(async () => {
            await errorCheck();
            last = await cdp.evaluate(statusExpression);
            return last === stepStatuses[name];
          }, observationDeadline, 'status');
        } catch (error) {
          await errorCheck();
          throw new Error(`${error.message}; expected ${JSON.stringify(stepStatuses[name])}, last read ${JSON.stringify(last)}`);
        }
      }
      if (name === 'R4' || name === 'R5') {
        let last = '';
        try {
          await owner.wait(async () => {
            await errorCheck();
            const ui = await cdp.evaluate(toastExpression);
            last = ui?.text ?? '';
            return ui && !ui.picker && ui.text.includes(refusalText) === (phase === 'toast');
          }, observationDeadline, 'toast/picker');
        } catch (error) {
          await errorCheck();
          throw new Error(`${error.message}; expected ${JSON.stringify(refusalText)} ${phase === 'toast' ? 'present' : 'absent'} and picker absent, last read ${JSON.stringify(last)}`);
        }
      }
      await owner.limit(errorCheck(), 1000, 'Suite error read');
      owner.check();
      await owner.limit(files.go(n), 1000, 'Step acknowledgement');
      deadline = Date.now() + (plan[n + 1]?.[2] ?? 15000);
      if (name === 'K2') console.log('[pass] K3 (control K2 established)');
      console.log(`${name}/${phase}: observed`);
    }
    context = cell.kind + '/exit';
    const exit = await owner.limit(owner.code.closed, 15000, 'VS Code exit');
    await owner.limit(errorCheck(), 1000, 'Final suite error read');
    owner.abort.signal.throwIfAborted();
    if (exit.code !== 0 || exit.signal) throw new Error('Exit: ' + JSON.stringify(exit));
  } catch (error) {
    error = new Error(`${context}: ${error.message}`);
    owner.abort.abort(error); // Terminal failure; cleanup never acknowledges a step.
    throw error;
  }
}

async function moduleEvidence(build) {
  const catalog = JSON.parse(await readFile(join(build, 'loka-web-apps.json'), 'utf8'));
  const app = catalog.apps?.[0];
  if (catalog.version !== 1 || !app || typeof app.js !== 'string' || !app.js.endsWith('.js')
      || isAbsolute(app.js) || app.js.split(/[\\/]/).includes('..')) throw new Error('Invalid T2 catalog');
  const wasm = await readFile(join(build, app.js.replace(/\.js$/, '.wasm')));
  console.log(`T2: tree=${build} apps[0]=${JSON.stringify({id: app.id, target: app.target})} wasm.sha256=${createHash('sha256').update(wasm).digest('hex').slice(0, 12)}`);
}

/** Cost: main once; at most three launches, each only after its predecessor succeeds. */
export async function launchCells(runtime, launch, report = console.log) {
  try { await launch('T3'); }
  catch (error) {
    report('[not run] R1 R3 R2 R4 R5: control K1 not established by a successful T3 launch');
    throw error;
  }
  await launch('R');
  if (runtime) await launch('T2');
}

export async function main(env = process.env) {
  let executable, invalid;
  try { executable = await validateInputs(env); } catch (error) { invalid = error.message; }
  const decision = prerequisiteDecision({platform: process.platform, node: process.versions.node,
    python: spawnSync('which', ['python3']).status === 0, xvfb: spawnSync('which', ['Xvfb']).status === 0, xauth: spawnSync('which', ['xauth']).status === 0,
    artifact: await optionalRead(join(extension, 'out/extension.js')) !== undefined, invalid}, env);
  decision.lines.forEach(line => console.log(line));
  if (decision.code) return decision.code;
  const buildRoot = join(repo, 'build');
  await mkdir(buildRoot, {recursive: true});
  if (await realpath(buildRoot) !== buildRoot) throw new Error('Build root must not be a symlink');
  const root = join(buildRoot, 'vscode-smoke');
  await mkdir(root, {recursive: true});
  if (await realpath(root) !== root) throw new Error('Smoke run root must not be a symlink');
  const owner = new RunOwner(await mkdtemp(join(root, `run-${process.pid}-`)));
  const cancel = signal => owner.abort.abort(new Error('Interrupted: ' + signal));
  const onINT = () => cancel('SIGINT'), onTERM = () => cancel('SIGTERM');
  process.on('SIGINT', onINT); process.on('SIGTERM', onTERM);
  const failures = [];
  try {
    const display = await owner.startX();
    await launchCells(!!env.LOKA_WEB_BUILD, async kind => {
      try {
        owner.abort.signal.throwIfAborted();
        if (kind === 'T2') await moduleEvidence(env.LOKA_WEB_BUILD);
        const directory = join(owner.directory, kind);
        const workspace = join(directory, 'ws');
        const buildDirectory = kind === 'T2' ? env.LOKA_WEB_BUILD : join(directory, 'missing-build');
        await mkdir(join(workspace, '.vscode'), {recursive: true});
        await mkdir(join(directory, 'user')); await mkdir(join(directory, 'ext'));
        await writeFile(join(workspace, '.vscode/settings.json'), JSON.stringify({'loka.preview.buildDirectory': buildDirectory, 'loka.preview.configureCommand': 'exit 3'}));
        owner.cellDirectory = directory;
        const args = [`--user-data-dir=${directory}/user`, `--extensions-dir=${directory}/ext`,
          `--extensionDevelopmentPath=${extension}`, `--extensionTestsPath=${here}/suite.cjs`,
          '--disable-gpu', '--remote-debugging-port=0', workspace];
        console.log(kind + ': argv=' + JSON.stringify([executable, ...args]));
        const childEnv = {...env, ...display, LOKA_SMOKE_CELL: directory, LOKA_SMOKE_KIND: kind};
        delete childEnv.WAYLAND_DISPLAY;
        const started = owner.startCode(executable, args, childEnv);
        await handshake(owner, {kind, workspace, buildDirectory, started}, {
          read: name => optionalRead(join(directory, name)),
          go: n => writeFile(join(directory, `go-${n}`), '', {flag: 'wx'})
        });
        console.log('[pass] ' + cells[kind]);
      } catch (error) { throw new Error(cells[kind] + ': ' + error.message); }
    });
  } catch (error) {
    failures.push(error.message);
  }
  finally {
    try { await owner.cleanup(); } catch (error) { failures.push(error.message); }
    process.off('SIGINT', onINT); process.off('SIGTERM', onTERM);
  }
  if (owner.abort.signal.aborted && !failures.length) failures.push(owner.abort.signal.reason.message);
  if (failures.length) throw new Error(failures.join('; '));
  return 0;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  // Bounded cleanup can leave live handles: exit hands them to the supervisor.
  main().then(code => process.exit(code), error => { console.error('[fail] vscode smoke: ' + error.message); process.exit(1); });
}
