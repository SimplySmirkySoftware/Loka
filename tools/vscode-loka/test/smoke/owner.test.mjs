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
const skip = process.env.LOKA_SKIP_SMOKE_PROCESS_TESTS === '1' ? 'explicit sandbox process-test exclusion' : missing;
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
  test('real owner: ' + variant, {skip: skip || false, timeout: 30000}, async t => {
    const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
    const parent = await mkdtemp(join(root, 'test-owner-'));
    const directory = join(parent, 'owned'), watch = join(parent, 'pids.jsonl');
    const supervisor = spawn('python3', [join(here, 'subreaper.py'), process.execPath,
      join(here, 'process-fixture.mjs'), 'owner'], {stdio: ['ignore', 'pipe', 'pipe'],
      env: {...process.env, LOKA_TEST_DIRECTORY: directory, LOKA_TEST_WATCH: watch, LOKA_TEST_VARIANT: variant}});
    let log = '';
    supervisor.stdout.on('data', data => { log += data; });
    supervisor.stderr.on('data', data => { log += data; });
    const closed = new Promise((resolve, reject) => { supervisor.on('error', reject); supervisor.on('close', code => resolve(code)); });
    let pids = [], ready;
    try {
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
    } finally {
      // The test alone cleans deliberately surviving controls; the production census only reports them.
      if (!pids.length) {
        try { pids = (await readFile(watch, 'utf8')).trim().split('\n').map(JSON.parse); }
        catch (error) { if (error.code !== 'ENOENT') throw error; }
      }
      for (const {pid} of pids) {
        try { process.kill(pid, 'SIGKILL'); } catch (error) { if (error.code !== 'ESRCH') throw error; }
      }
      // The driver holds its child handle, so it can only finish once the killed controls are gone.
      // Give it that chance before the last-resort signal, which would turn a passing run into exit 241.
      if (ready) {
        await until(() => absent(`/proc/${ready.pid}`), 5000).catch(() => {
          try { process.kill(ready.pid, 'SIGTERM'); } catch (error) { if (error.code !== 'ESRCH') throw error; }
        });
      }
      await until(async () => (await Promise.all(pids.map(p => absent(`/proc/${p.pid}`)))).every(Boolean));
      assert.equal(await closed, 0, log);
      await rm(parent, {recursive: true, force: true});
    }
  });
}
test('real owner: concurrent display allocation and ownership', {skip: skip || false, timeout: 30000}, async () => {
  const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
  const owners = await Promise.all([0, 1].map(async () => new RunOwner(await mkdtemp(join(root, 'test-display-')))));
  try {
    const displays = await Promise.all(owners.map(owner => owner.startX()));
    assert.notEqual(displays[0].DISPLAY, displays[1].DISPLAY);
    for (const owner of owners) {
      assert.equal(Number(await readFile(`/tmp/.X${owner.x.display}-lock`, 'utf8')), owner.x.child.pid);
    }
  } finally {
    const results = await Promise.allSettled(owners.map(owner => owner.cleanup()));
    for (const result of results) if (result.status === 'rejected') throw result.reason;
  }
});
if (skip) console.log('[skip] real owner timeout, SIGTERM, group residual, escaped residual, concurrent display: ' + skip);
