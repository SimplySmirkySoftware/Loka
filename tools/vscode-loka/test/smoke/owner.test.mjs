import test from 'node:test';
import assert from 'node:assert/strict';
import {spawn, spawnSync} from 'node:child_process';
import {mkdir, mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import {join, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';
import {RunOwner, repo} from './run.mjs';
const here = dirname(fileURLToPath(import.meta.url));
const missing = process.platform !== 'linux' ? 'Linux absent'
  : ['Xvfb', 'xauth', 'python3'].filter(tool => spawnSync('which', [tool]).status !== 0).join(', ');
const required = process.env.LOKA_REQUIRE_SMOKE_PROCESS_TESTS === '1';
const exclusion = process.env.LOKA_SKIP_SMOKE_PROCESS_TESTS === '1' ? 'explicit sandbox process-test exclusion' : missing;
const skip = required ? false : exclusion;
function prerequisites() {
  assert.ok(!exclusion, 'Required smoke process tests unavailable: ' + exclusion);
}
async function absent(path) {
  try { await stat(path); return false; } catch (error) { if (error.code === 'ENOENT') return true; throw error; }
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
async function json(path) {
  try { return JSON.parse(await readFile(path, 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}
for (const variant of ['timeout', 'signal', 'group-residual', 'escaped-residual']) {
  test('real owner: ' + variant, {skip: skip || false, timeout: 60000}, async t => {
    prerequisites();
    const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
    const parent = await mkdtemp(join(root, 'test-owner-'));
    const directory = join(parent, 'owned'), watch = join(parent, 'pids.jsonl');
    const supervisor = spawn('python3', [join(here, 'subreaper.py'), process.execPath,
      join(here, 'process-fixture.mjs'), 'owner'], {stdio: ['ignore', 'pipe', 'pipe', 'pipe'],
      env: {...process.env, LOKA_TEST_DIRECTORY: directory, LOKA_TEST_WATCH: watch, LOKA_TEST_VARIANT: variant}});
    let log = '';
    supervisor.stdout.on('data', data => { log += data; });
    supervisor.stderr.on('data', data => { log += data; });
    let closure, driverText = '';
    supervisor.stdio[3].on('data', data => { driverText += data; });
    supervisor.on('error', error => { closure = {error}; });
    supervisor.on('close', code => { closure = {...closure, code}; });
    const driverPid = () => /^\d+\n$/.test(driverText) ? Number(driverText) : undefined;
    const failures = [];
    const attempt = async operation => {
      try { await operation(); } catch (error) { failures.push(error); }
    };
    const kill = (pid, signal) => {
      if (!pid) return;
      try { process.kill(pid, signal); } catch (error) { if (error.code !== 'ESRCH') throw error; }
    };
    let pids = [], ready;
    try {
      await until(() => driverPid() || closure && Promise.reject(closure.error || new Error(log)));
      ready = await until(() => json(join(parent, 'ready.json')));
      pids = (await readFile(watch, 'utf8')).trim().split('\n').map(JSON.parse);
      assert.deepEqual(pids.map(p => p.role).sort(), ['child', 'code', 'grandchild']);
      // This watcher uses fixture-written PIDs, never the owner's roll-call result.
      for (const {pid} of pids) assert.equal(await absent(`/proc/${pid}`), false);
      const groupOf = async pid => {
        const value = await readFile(`/proc/${pid}/stat`, 'utf8');
        return value.slice(value.lastIndexOf(')') + 2).split(' ')[2];
      };
      assert.equal(await groupOf(pids[0].pid), await groupOf(pids[1].pid));
      assert.notEqual(await groupOf(pids[0].pid), await groupOf(pids[2].pid));
      if (variant === 'signal') process.kill(ready.pid, 'SIGTERM');
      const outcome = await until(() => json(join(parent, 'outcome.json')));
      assert.match(outcome.errors.join('\n'), variant === 'signal' ? /SIGTERM/ : /fake release timed out/);
      assert.equal(await absent(directory), true);
      assert.equal(await absent(`/proc/${ready.xpid}`), true);
      const lock = await readFile(`/tmp/.X${ready.display}-lock`, 'utf8').catch(error => {
        if (error.code === 'ENOENT') return ''; throw error;
      });
      assert.notEqual(Number(lock), ready.xpid);
      if (variant.endsWith('residual')) {
        assert.match(outcome.errors.join('\n'), /Code roll call timed out/);
        const survivor = variant === 'escaped-residual' ? pids[2] : pids[0];
        assert.equal(await absent(`/proc/${survivor.pid}`), false);
        assert.ok(outcome.errors.join('\n').includes(`"pid":${survivor.pid}`));
        t.diagnostic('Negative control detected residual PID ' + survivor.pid);
      } else {
        await until(async () => (await Promise.all(pids.map(p => absent(`/proc/${p.pid}`)))).every(Boolean));
        assert.doesNotMatch(outcome.errors.join('\n'), /roll call|Xvfb/);
      }
    } catch (error) {
      failures.push(error);
    } finally {
      try {
        // Assertions above observe residuals before any corrective kill.
        await attempt(async () => {
          try {
            const lines = (await readFile(watch, 'utf8')).trim().split('\n').filter(Boolean);
            for (const line of lines) await attempt(async () => {
              const entry = JSON.parse(line);
              if (!pids.some(p => p.pid === entry.pid)) pids.push(entry);
            });
          } catch (error) { if (error.code !== 'ENOENT') throw error; }
        });
      } finally {
        try {
          for (const {pid} of pids) await attempt(() => kill(pid, 'SIGKILL'));
          // Keep the chance for normal driver exit before last-resort SIGTERM.
          await attempt(async () => {
            const pid = driverPid();
            if (pid) await until(() => absent(`/proc/${pid}`), 5000).catch(() => kill(pid, 'SIGTERM'));
          });
          await attempt(async () => {
            await until(() => closure, 5000);
            if (closure.error) throw closure.error;
            assert.equal(closure.code, 0, log);
          });
        } finally {
          try {
            if (!closure) {
              // The subreaper also owns descendants never recorded during failed setup.
              await attempt(() => kill(supervisor.pid, 'SIGTERM'));
              await attempt(() => until(() => closure, 5000));
            }
            if (!closure) {
              await attempt(() => kill(supervisor.pid, 'SIGKILL'));
              await attempt(() => until(() => closure, 1000));
            }
            await attempt(() => until(async () =>
              (await Promise.all(pids.map(p => absent(`/proc/${p.pid}`)))).every(Boolean), 1000));
          } finally {
            supervisor.stdout.destroy();
            supervisor.stderr.destroy();
            supervisor.stdio[3].destroy();
            await attempt(() => rm(parent, {recursive: true, force: true}));
          }
        }
      }
    }
    if (failures.length) throw new AggregateError(failures, failures.map(error => error.message).join('\n'));
  });
}
test('real owner: concurrent display allocation and ownership', {skip: skip || false, timeout: 30000}, async () => {
  prerequisites();
  const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
  const owners = [], failures = [];
  try {
    for (let i = 0; i < 2; i++) owners.push(new RunOwner(await mkdtemp(join(root, 'test-display-'))));
    const displays = await Promise.all(owners.map(owner => owner.startX()));
    assert.notEqual(displays[0].DISPLAY, displays[1].DISPLAY);
    for (const owner of owners) {
      assert.equal(Number(await readFile(`/tmp/.X${owner.x.display}-lock`, 'utf8')), owner.x.child.pid);
    }
  } catch (error) {
    failures.push(error);
  } finally {
    const results = await Promise.allSettled(owners.map(owner => owner.cleanup()));
    for (const result of results) if (result.status === 'rejected') failures.push(result.reason);
  }
  if (failures.length) throw new AggregateError(failures, failures.map(error => error.message).join('\n'));
});
if (skip) console.log('[skip] real owner timeout, SIGTERM, group residual, escaped residual, concurrent display: ' + skip);
