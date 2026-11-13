#!/usr/bin/env node
import {spawn, spawnSync} from 'node:child_process';
import {constants} from 'node:fs';
import {access, mkdir, mkdtemp, readFile, readdir, realpath, rename, rm, stat, writeFile} from 'node:fs/promises';
import {createHash, randomBytes} from 'node:crypto';
import {dirname, isAbsolute, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export const repo = resolve(here, '../../../..');
const extension = resolve(here, '../..');
export const statuses = Object.freeze({R: 'Loka: Trust the workspace to preview',
  T3: 'Loka: Web catalog missing; run Configure', T2: 'Loka: Running'});
const cells = {R: 'R1 R2', T3: 'T1 T3', T2: "T1' T2"};
const allCells = "R1 R2 T1 T3 T1' T2";
const manifest = JSON.parse(await readFile(join(extension, 'package.json'), 'utf8'));
const commands = manifest.contributes.commands.map(item => item.command);
export const pin = JSON.parse(await readFile(join(here, 'vscode-pin.json'), 'utf8'));

/** Pure decision; malformed/missing records never become success. */
export function judge(cell, result, status, exit) {
  const errors = [];
  if (!result || typeof result !== 'object' || Array.isArray(result)) return ['Missing or malformed result.json'];
  if (Object.hasOwn(result, 'error')) errors.push('Suite error: ' + result.error);
  if (result.active !== true || !Array.isArray(result.commands)
      || commands.length !== 6 || commands.some(id => !result.commands.includes(id))) errors.push('R1 activation/commands');
  if (result.trustEnabled !== true || result.initialTrusted !== false) errors.push('Initial Workspace Trust');
  if (result.finalTrusted !== (cell.kind !== 'R') || result.trustGranted !== (cell.kind !== 'R')) errors.push('Trust outcome');
  if (!Array.isArray(result.folders) || result.folders.length !== 1 || result.folders[0] !== cell.workspace) errors.push('Workspace folder');
  if (result.buildDirectory !== cell.buildDirectory) errors.push('Effective buildDirectory');
  if (!Array.isArray(result.tabs) || result.tabs.filter(label => label === 'Loka Preview').length !== 1) errors.push('Preview tab');
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
    !facts.xvfb && 'Xvfb', !facts.xauth && 'xauth', !env.LOKA_VSCODE && 'LOKA_VSCODE not set',
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

/** /proc is the ownership census, not a best-effort diagnostic. Only vanished PIDs are ignored. */
export async function rollCall(group, directory, xpid, fs = {readdir, readFile}) {
  const residual = [];
  for (const name of await fs.readdir('/proc')) {
    if (!/^\d+$/.test(name)) continue;
    try {
      const value = await fs.readFile(`/proc/${name}/stat`, 'utf8');
      const fields = value.slice(value.lastIndexOf(')') + 2).split(' ');
      const cmd = (await fs.readFile(`/proc/${name}/cmdline`, 'utf8')).replaceAll('\0', ' ');
      if (Number(name) !== xpid && ((group && Number(fields[2]) === group) || cmd.includes(directory))) {
        residual.push({pid: Number(name), name: value.slice(value.indexOf('(') + 1, value.lastIndexOf(')')), cmd});
      }
    } catch (error) { if (error.code !== 'ENOENT' && error.code !== 'ESRCH') throw error; }
  }
  return residual;
}

function childRecord(child) {
  const record = {child, outcome: undefined};
  record.closed = new Promise(resolve => {
    child.once('error', error => { record.error = error; });
    child.once('close', (code, signal) => { record.outcome = {code, signal}; resolve(record.outcome); });
  });
  return record;
}
function kill(pid, signal) {
  try { process.kill(pid, signal); } catch (error) { if (error.code !== 'ESRCH') throw error; }
}

/** Owns the run directory, X server, code group, CDP and every launcher timer.
 * Parallel to tests/web/driver/capture.mjs runBrowser: this owner also holds a file handshake. */
export class RunOwner {
  constructor(directory, {graceMs = 15000, censusMs = 5000, killGroup = kill} = {}) {
    this.directory = directory;
    this.graceMs = graceMs;
    this.censusMs = censusMs;
    this.killGroup = killGroup;
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
    if (this.code?.outcome) throw new Error('VS Code exited early: ' + JSON.stringify(this.code.outcome));
    if (this.x?.error) throw this.x.error;
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
      const lock = `/tmp/.X${display}-lock`;
      if (await optionalRead(lock) !== undefined) continue;
      const result = spawnSync('xauth', ['-f', auth, 'add', ':' + display, '.', randomBytes(16).toString('hex')],
        {timeout: Math.max(1, deadline - Date.now()), stdio: 'pipe'});
      if (result.error || result.status !== 0) throw new Error('xauth failed: ' + (result.error || result.stderr));
      this.x = childRecord(spawn('Xvfb', [':' + display, '-nolisten', 'tcp', '-auth', auth,
        '-screen', '0', '1280x800x24'], {stdio: verbose}));
      this.x.display = display;
      const acquired = await this.wait(async () => {
        this.abort.signal.throwIfAborted();
        if (this.x.error) throw this.x.error;
        const owner = Number(await optionalRead(lock));
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
  async release() {
    if (!this.cellDirectory) return;
    await writeFile(join(this.cellDirectory, 'release.tmp'), 'release');
    await rename(join(this.cellDirectory, 'release.tmp'), join(this.cellDirectory, 'release'));
  }
  async cleanup() {
    const errors = [];
    const attempt = async action => { try { await action(); } catch (error) { errors.push(error.message); } };
    await attempt(() => this.release());
    if (this.code) {
      await attempt(async () => {
        if (!this.code.outcome) {
          try { await this.limit(this.code.closed, this.graceMs, 'Code release'); }
          catch (error) { errors.push(error.message); }
        }
        // Kill even after the leader exits: children can keep its group alive.
        if (this.code.child.pid) this.killGroup(-this.code.child.pid, 'SIGKILL');
        await this.limit(this.code.closed, 5000, 'Code close');
      });
    }
    await attempt(async () => {
      let residual = [];
      try {
        await this.wait(async () => {
          residual = await rollCall(this.code?.child.pid, this.directory, this.x?.child.pid);
          return residual.length === 0;
        }, Date.now() + this.censusMs, 'Code roll call', false);
      } catch (error) { throw new Error(error.message + ': ' + JSON.stringify(residual)); }
    });
    await attempt(async () => {
      if (!this.x) return;
      const pid = this.x.child.pid;
      if (pid && !this.x.outcome) {
        kill(pid, 'SIGTERM');
        try { await this.limit(this.x.closed, 5000, 'Xvfb close'); }
        catch (error) {
          errors.push(error.message);
          kill(pid, 'SIGKILL');
          await this.limit(this.x.closed, 5000, 'Xvfb killed close');
        }
      }
    });
    await attempt(async () => {
      if (!this.x?.child.pid) return;
      const pid = this.x.child.pid;
      const live = await optionalRead(`/proc/${pid}/stat`);
      const lock = Number(await optionalRead(`/tmp/.X${this.x.display}-lock`));
      if (live !== undefined || lock === pid) throw new Error(`X roll call residual pid=${pid} display=${this.x.display}`);
    });
    await attempt(async () => this.cdp?.close());
    await attempt(() => rm(this.directory, {recursive: true, force: true}));
    for (const timer of this.timers) clearTimeout(timer);
    this.timers.clear();
    if (errors.length) throw new Error('Cleanup failed: ' + errors.join('; '));
  }
}

// Only these two expressions inspect workbench DOM; pin upgrades must recheck them.
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

/** Injected file/CDP ports exercise the actual order and deadlines without VS Code. */
export async function handshake(owner, cell, files, connect = connectCDP) {
  const deadline = cell.started + 40000;
  if (cell.kind !== 'R') await owner.wait(() => files.read('ready'), deadline, 'ready');
  const cdp = await connect(owner, owner.cellDirectory, deadline);
  if (cell.kind !== 'R') await owner.wait(() => cdp.evaluate(trustExpression), Date.now() + 10000, 'Trust button');
  const raw = await owner.wait(() => files.read('result.json'), deadline, 'result.json');
  let result;
  try { result = JSON.parse(raw); } catch { throw new Error('Malformed result.json'); }
  const invalid = judge(cell, result, statuses[cell.kind], 0);
  if (invalid.length) throw new Error(invalid.join('; '));
  let seen;
  const status = await owner.wait(async () => {
    const value = seen = await cdp.evaluate(statusExpression);
    return value === statuses[cell.kind] && value;
  }, Date.now() + 10000, 'status').catch(error => {
    throw new Error(`${error.message}; expected ${JSON.stringify(statuses[cell.kind])}, last read ${JSON.stringify(seen)}`);
  });
  console.log(`${cell.kind}: status=${JSON.stringify(status)} trust.enabled=${result.trustEnabled}`);
  owner.check();
  await files.release();
  const exit = await owner.limit(owner.code.closed, 15000, 'VS Code exit');
  const errors = judge(cell, result, status, exit.code);
  if (errors.length) throw new Error(errors.join('; '));
}

async function moduleEvidence(build) {
  const catalog = JSON.parse(await readFile(join(build, 'loka-web-apps.json'), 'utf8'));
  const app = catalog.apps?.[0];
  if (catalog.version !== 1 || !app || typeof app.js !== 'string' || !app.js.endsWith('.js')
      || isAbsolute(app.js) || app.js.split(/[\\/]/).includes('..')) throw new Error('Invalid T2 catalog');
  const wasm = await readFile(join(build, app.js.replace(/\.js$/, '.wasm')));
  console.log(`T2: tree=${build} apps[0]=${JSON.stringify({id: app.id, target: app.target})} wasm.sha256=${createHash('sha256').update(wasm).digest('hex').slice(0, 12)}`);
}

export async function main(env = process.env) {
  let executable, invalid;
  try { executable = await validateInputs(env); } catch (error) { invalid = error.message; }
  const decision = prerequisiteDecision({platform: process.platform, node: process.versions.node,
    xvfb: spawnSync('which', ['Xvfb']).status === 0, xauth: spawnSync('which', ['xauth']).status === 0,
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
    for (const kind of ['R', 'T3', ...(env.LOKA_WEB_BUILD ? ['T2'] : [])]) {
      try {
        owner.abort.signal.throwIfAborted();
        if (kind === 'T2') await moduleEvidence(env.LOKA_WEB_BUILD);
        const directory = join(owner.directory, kind);
        const workspace = join(directory, 'ws');
        const buildDirectory = kind === 'T2' ? env.LOKA_WEB_BUILD : join(directory, 'missing-build');
        await mkdir(join(workspace, '.vscode'), {recursive: true});
        await mkdir(join(directory, 'user')); await mkdir(join(directory, 'ext'));
        await writeFile(join(workspace, '.vscode/settings.json'), JSON.stringify({'loka.preview.buildDirectory': buildDirectory}));
        owner.cellDirectory = directory;
        const args = [`--user-data-dir=${directory}/user`, `--extensions-dir=${directory}/ext`,
          `--extensionDevelopmentPath=${extension}`, `--extensionTestsPath=${here}/suite.cjs`,
          '--disable-gpu', '--remote-debugging-port=0', workspace];
        console.log(kind + ': argv=' + JSON.stringify([executable, ...args]));
        const childEnv = {...env, ...display, LOKA_SMOKE_CELL: directory, LOKA_SMOKE_KIND: kind};
        delete childEnv.WAYLAND_DISPLAY;
        const started = owner.startCode(executable, args, childEnv);
        await handshake(owner, {kind, workspace, buildDirectory, started}, {
          read: name => optionalRead(join(directory, name)), release: () => owner.release()
        });
        // Keep the completed group until its census succeeds, before replacing its record.
        await owner.wait(async () => (await rollCall(owner.code.child.pid, owner.directory, owner.x.child.pid)).length === 0,
          Date.now() + 5000, 'Cell roll call', false);
        console.log('[pass] ' + cells[kind]);
      } catch (error) { throw new Error(cells[kind] + ': ' + error.message); }
    }
  } catch (error) { failures.push(error.message); }
  finally {
    try { await owner.cleanup(); } catch (error) { failures.push(error.message); }
    process.off('SIGINT', onINT); process.off('SIGTERM', onTERM);
  }
  if (owner.abort.signal.aborted) failures.push(owner.abort.signal.reason.message);
  if (failures.length) throw new Error(failures.join('; '));
  return 0;
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().then(code => { process.exitCode = code; }, error => { console.error('[fail] vscode smoke: ' + error.message); process.exitCode = 1; });
}
