// Executed only by owner.test.mjs; never a VS Code replacement in the smoke entry.
import {spawn} from 'node:child_process';
import {appendFileSync, readFileSync} from 'node:fs';
import {mkdir, readFile, rename, writeFile} from 'node:fs/promises';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {RunOwner} from './run.mjs';
const self = fileURLToPath(import.meta.url);
async function publish(path, value) {
  await writeFile(path + '.tmp', JSON.stringify(value));
  await rename(path + '.tmp', path);
}
const [mode, directory = process.env.LOKA_TEST_DIRECTORY, watch = process.env.LOKA_TEST_WATCH,
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
} else {
  const owner = new RunOwner(directory, {graceMs: 100, censusMs: 500,
    ...(variant === 'group-residual' ? {killGroup() {}} : {})});
  const cancel = () => owner.abort.abort(new Error('Interrupted: SIGTERM'));
  process.on('SIGTERM', cancel);
  const errors = [];
  try {
    await mkdir(directory);
    owner.cellDirectory = directory;
    await owner.startX();
    owner.startCode(process.execPath, [self, 'code', directory, watch, variant], process.env);
    await owner.wait(async () => {
      try { return (await readFile(watch, 'utf8')).trim().split('\n').length === 3; }
      catch (error) { if (error.code === 'ENOENT') return false; throw error; }
    }, Date.now() + 5000, 'fake descendants ready');
    await publish(join(directory, '../ready.json'), {pid: process.pid,
      xpid: owner.x.child.pid, display: owner.x.display});
    await owner.wait(() => false, Date.now() + (variant === 'signal' ? 10000 : 100), 'fake release');
  } catch (error) { errors.push(error.message); }
  finally {
    try { await owner.cleanup(); } catch (error) { errors.push(error.message); }
    process.off('SIGTERM', cancel);
    await publish(join(directory, '../outcome.json'), {errors});
  }
}
