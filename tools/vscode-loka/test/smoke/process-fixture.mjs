// Executed only by owner.test.mjs; never a VS Code replacement in the smoke entry.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {appendFileSync, readFileSync} from 'node:fs';
import {mkdir, mkdtemp, readFile, rename, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {RunOwner} from './run.mjs';
const self = fileURLToPath(import.meta.url);
async function publish(path, value) {
  await writeFile(path + '.tmp', JSON.stringify(value));
  await rename(path + '.tmp', path);
}
const [mode, directory = process.env.LOKA_SMOKE_RUN_ROOT, watch = process.env.LOKA_TEST_WATCH,
  variant = process.env.LOKA_TEST_VARIANT, leader] = process.argv.slice(2);
if (['code', 'child', 'grandchild'].includes(mode)) {
  appendFileSync(watch, JSON.stringify({role: mode, pid: process.pid}) + '\n');
  if (mode !== 'grandchild') {
    const child = spawn(process.execPath, [self, mode === 'code' ? 'child' : 'grandchild', directory,
      watch, variant, mode === 'code' ? String(process.pid) : leader],
    {detached: mode === 'child', stdio: 'ignore'});
    child.unref();
  }
  setInterval(() => {
    if (mode !== 'grandchild' || variant === 'escaped-residual') return;
    // Mirrors crashpad's self-exit after its main process dies, including a zombie leader.
    try {
      const stat = readFileSync(`/proc/${leader}/stat`, 'utf8');
      if (stat.slice(stat.lastIndexOf(')') + 2).startsWith('Z ')) process.exit(0);
    } catch (error) { if (error.code === 'ENOENT') process.exit(0); else throw error; }
  }, 20);
} else if (mode === 'displays') {
  const owners = [];
  try {
    for (let i = 0; i < 2; i++) owners.push(new RunOwner(await mkdtemp(join(process.env.LOKA_SMOKE_RUN_ROOT, 'display-'))));
    const displays = await Promise.all(owners.map(owner => owner.startX()));
    assert.notEqual(displays[0].DISPLAY, displays[1].DISPLAY);
    for (const owner of owners)
      assert.equal(Number(await readFile(`/tmp/.X${owner.x.display}-lock`, 'utf8')), owner.x.child.pid);
  } finally {
    const results = await Promise.allSettled(owners.map(owner => owner.cleanup()));
    for (const result of results) if (result.status === 'rejected') throw result.reason;
  }
} else {
  const owner = new RunOwner(directory);
  const cancel = () => owner.abort.abort(new Error('Interrupted: SIGTERM'));
  process.on('SIGTERM', cancel);
  const errors = [];
  try {
    await mkdir(directory, {recursive: true});
    owner.cellDirectory = directory;
    await owner.startX();
    owner.startCode(process.execPath, [self, 'code', directory, watch, variant], process.env);
    await owner.wait(async () => {
      try { return (await readFile(watch, 'utf8')).trim().split('\n').length === 3; }
      catch (error) { if (error.code === 'ENOENT') return false; throw error; }
    }, Date.now() + 5000, 'fake descendants ready');
    await publish(join(process.env.LOKA_TEST_DIRECTORY, '../ready.json'), {pid: process.pid, directory,
      xpid: owner.x.child.pid, display: owner.x.display});
    await owner.wait(() => false, Date.now() + (['signal', 'launcher-kill'].includes(variant) ? 10000 : 100), 'fake step');
  } catch (error) { errors.push(error.message); }
  finally {
    try { await owner.cleanup(); } catch (error) { errors.push(error.message); }
    process.off('SIGTERM', cancel);
    process.exitCode = 1;
    await publish(join(process.env.LOKA_TEST_DIRECTORY, '../outcome.json'), {errors});
  }
}
