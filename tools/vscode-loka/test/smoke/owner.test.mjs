import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdir, mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import {join, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {repo, pidAlive} from './run.mjs';
const here = dirname(fileURLToPath(import.meta.url));
function exclusion(tools) {
  return process.env.LOKA_SKIP_SMOKE_PROCESS_TESTS === '1' ? 'explicit sandbox process-test exclusion'
    : process.platform !== 'linux' ? 'Linux absent'
    : tools.filter(tool => spawnSync('which', [tool]).status !== 0).join(', ');
}
function options(name, tools) {
  const missing = exclusion(tools);
  if (missing) console.log(`[${process.env.LOKA_REQUIRE_SMOKE_PROCESS_TESTS === '1' ? 'fail' : 'skip'}] ${name}: ${missing}`);
  return {skip: process.env.LOKA_REQUIRE_SMOKE_PROCESS_TESTS === '1' ? false : missing || false, timeout: 65000};
}
async function until(read, ms = 15000) {
  const deadline = Date.now() + ms;
  for (;;) {
    const value = await read();
    if (value) return value;
    if (Date.now() >= deadline) throw new Error('Independent watch timed out');
    await new Promise(resolve => setTimeout(resolve, 20));
  }
}
async function absent(path) {
  try { await stat(path); return false; } catch (error) { if (error.code === 'ENOENT') return true; throw error; }
}
async function json(path) {
  try { return JSON.parse(await readFile(path, 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}
function supervised(args, env = {}) {
  const child = spawn('python3', [join(here, 'supervisor.py'), ...args],
    {env: {...process.env, LOKA_REQUIRE_SMOKE_PROCESS_TESTS: '1', ...env}, stdio: ['ignore', 'pipe', 'pipe']});
  let log = '';
  child.stdout.on('data', data => { log += data; }); child.stderr.on('data', data => { log += data; });
  const closed = new Promise((resolve, reject) => {
    child.once('error', reject); child.once('close', code => resolve({code, log}));
  });
  return {child, closed};
}
for (const variant of ['a', 'b', 'c', 'd', 'e', 'stuck', 'launcher-kill']) {
  const name = 'real supervisor control ' + variant;
  test(name, options(name, ['python3']), async () => {
    assert.equal(exclusion(['python3']), '');
    const directory = await mkdtemp('/tmp/loka-supervisor-test-');
    const run = supervised(['python3', join(here, 'supervisor-fixture.py'), 'watch', variant, directory]);
    try {
      const result = await run.closed;
      assert.equal(result.code, 0, result.log);
      if (variant !== 'a') assert.match(result.log, /independent pidfd observation passed/);
    } finally { await rm(directory, {recursive: true, force: true}); }
  });
}
for (const variant of ['timeout', 'signal', 'escaped-residual', 'launcher-kill']) {
  const name = 'real owner: ' + variant;
  test(name, options(name, ['Xvfb', 'xauth', 'python3']), async () => {
    assert.equal(exclusion(['Xvfb', 'xauth', 'python3']), '');
    const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
    const parent = await mkdtemp(join(root, 'test-owner-'));
    const directory = join(parent, 'owned'), watch = join(parent, 'pids.jsonl');
    const run = supervised([process.execPath, join(here, 'process-fixture.mjs'), 'owner'],
      {LOKA_TEST_DIRECTORY: directory, LOKA_TEST_WATCH: watch, LOKA_TEST_VARIANT: variant});
    let ready;
    try {
      ready = await until(() => json(join(parent, 'ready.json')));
      const pids = (await readFile(watch, 'utf8')).trim().split('\n').map(JSON.parse);
      assert.deepEqual(pids.map(p => p.role).sort(), ['child', 'code', 'grandchild']);
      for (const {pid} of pids) assert.equal(await absent(`/proc/${pid}`), false);
      if (variant === 'signal') run.child.kill('SIGTERM');
      if (variant === 'launcher-kill') process.kill(ready.pid, 'SIGKILL');
      const result = await run.closed;
      assert.notEqual(result.code, 0, result.log);
      await until(async () => (await Promise.all([...pids.map(p => p.pid), ready.xpid]
        .map(pid => absent(`/proc/${pid}`)))).every(Boolean));
      if (variant !== 'launcher-kill') {
        assert.equal(await absent(directory), true);
        const outcome = await json(join(parent, 'outcome.json'));
        assert.match(outcome.errors.join('\n'), variant === 'signal' ? /SIGTERM/ : /fake step timed out/);
      }
    } finally {
      if (run.child.exitCode === null && run.child.signalCode === null) run.child.kill('SIGTERM');
      await run.closed;
      if (variant === 'launcher-kill' && ready && !pidAlive(ready.xpid)) {
        const lock = `/tmp/.X${ready.display}-lock`;
        try {
          if (Number(await readFile(lock, 'utf8')) === ready.xpid) await rm(lock);
        } catch (error) { if (error.code !== 'ENOENT') throw error; }
      }
      await rm(parent, {recursive: true, force: true});
    }
  });
}
test('real owner: concurrent display allocation and ownership',
  options('concurrent display', ['Xvfb', 'xauth', 'python3']), async () => {
    assert.equal(exclusion(['Xvfb', 'xauth', 'python3']), '');
    const result = await supervised([process.execPath, join(here, 'process-fixture.mjs'), 'displays']).closed;
    assert.equal(result.code, 0, result.log);
  });
