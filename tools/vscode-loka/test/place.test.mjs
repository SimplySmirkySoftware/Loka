import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

const script = new URL('../scripts/place.cjs', import.meta.url);
// File-backed capture also works in sandboxes which refuse Node's socket-backed pipes.
function runNode(args, options = {}) {
  const logs = fs.mkdtempSync(path.join(os.tmpdir(), 'loka-place-output-'));
  const out = fs.openSync(path.join(logs, 'stdout'), 'w');
  const err = fs.openSync(path.join(logs, 'stderr'), 'w');
  try {
    const result = spawnSync(process.execPath, args, {...options, stdio: ['ignore', out, err]});
    assert.ifError(result.error);
    return {...result, stdout: fs.readFileSync(path.join(logs, 'stdout'), 'utf8'),
      stderr: fs.readFileSync(path.join(logs, 'stderr'), 'utf8')};
  } finally {
    fs.closeSync(out); fs.closeSync(err); fs.rmSync(logs, {recursive: true, force: true});
  }
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'loka-place-test-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const extension = path.join(root, 'extension'), workspace = path.join(root, 'workspace');
  fs.mkdirSync(path.join(extension, 'scripts'), {recursive: true});
  fs.mkdirSync(workspace);
  fs.copyFileSync(script, path.join(extension, 'scripts/place.cjs'));
  const manifest = {name: 'loka-preview', publisher: 'simplysmirkysoftware', main: './out/extension.js',
    scripts: {build: 'node -e "process.exit(3)"'}};
  fs.writeFileSync(path.join(extension, 'package.json'), JSON.stringify(manifest));
  const payload = path.join(root, 'payload');
  fs.mkdirSync(path.join(payload, 'src'), {recursive: true});
  fs.writeFileSync(path.join(payload, 'extension.js'), "require('vscode'); module.exports = require('./src/wire');\n");
  fs.writeFileSync(path.join(payload, 'src/wire.js'), "module.exports = require('./cycle');\n");
  fs.writeFileSync(path.join(payload, 'src/cycle.js'), "require('./wire'); module.exports = 'A';\n");
  const dst = path.join(workspace, '.vscode/extensions/loka-preview');
  const lock = path.join(extension, '.place.lock');
  const run = (args = [], fail, built = true, target = workspace) => runNode(
    [path.join(extension, 'scripts/place.cjs'), ...(built ? ['--test-payload', payload] : []), ...args, target],
    {encoding: 'utf8', env: {...process.env, LOKA_PLACE_TEST_FAIL: fail || ''}});
  const place = () => { const r = run(); assert.equal(r.status, 0, r.stdout + r.stderr); return JSON.parse(fs.readFileSync(path.join(dst, 'package.json'))); };
  const unlock = () => fs.rmSync(lock, {recursive: true, force: true});
  return {root, extension, workspace, payload, dst, lock, run, place, unlock};
}
// Independent execution oracle: real Node resolves the entire transitive graph, with only vscode stubbed.
function load(dst, manifest, expected) {
  const r = runNode(['-e', `const M=require('node:module'); const old=M._load;
    M._load=function(id,...rest){ return id==='vscode'?{}:old.call(this,id,...rest); };
    require('node:assert/strict').equal(require(process.argv[1]),process.argv[2]);`,
  path.resolve(dst, manifest.main), expected], {encoding: 'utf8'});
  assert.equal(r.status, 0, r.stderr);
}
function snapshot(directory) {
  return fs.readdirSync(directory, {withFileTypes: true}).sort((a,b) => a.name.localeCompare(b.name)).map(e =>
    [e.name, e.isDirectory() ? snapshot(path.join(directory, e.name)) : e.isSymbolicLink()
      ? fs.readlinkSync(path.join(directory, e.name)) : fs.readFileSync(path.join(directory, e.name), 'hex')]);
}
const failures = ['after-build', 'mid-copy', 'after-publish', 'before-commit', 'after-commit'];
test('old and current manifests remain loadable across B/C and every interrupted placement', t => {
  const f = fixture(t), a = f.place();
  for (const version of ['B', 'C']) {
    fs.writeFileSync(path.join(f.payload, 'src/cycle.js'), `require('./wire'); module.exports = '${version}';\n`);
    for (const point of failures) {
      const before = JSON.parse(fs.readFileSync(path.join(f.dst, 'package.json')));
      const r = f.run([], point); assert.equal(r.status, 86, r.stderr);
      assert.ok(fs.existsSync(f.lock));
      const current = JSON.parse(fs.readFileSync(path.join(f.dst, 'package.json')));
      if (point !== 'after-commit') assert.deepEqual(current, before);
      load(f.dst, a, 'A');
      load(f.dst, current, current.main === a.main ? 'A' : version === 'C' && point !== 'after-commit' ? 'B' : version);
      f.unlock();
    }
    load(f.dst, f.place(), version); load(f.dst, a, 'A');
  }
});
test('lock refuses placement and clean without changing destination', t => {
  const f = fixture(t); f.place(); fs.mkdirSync(f.lock);
  const before = snapshot(f.dst);
  for (const clean of [false, true]) {
    const r = f.run(clean ? ['--clean'] : [], undefined, !clean);
    assert.equal(r.status, 1); assert.match(r.stderr, /Another place.*remove/);
    assert.deepEqual(snapshot(f.dst), before);
  }
});
test('missing transitive require and failed build preserve manifest and published files', t => {
  const f = fixture(t), a = f.place(), before = snapshot(f.dst);
  fs.unlinkSync(path.join(f.payload, 'src/wire.js'));
  assert.equal(f.run().status, 1); assert.deepEqual(snapshot(f.dst), before);
  const r = f.run([], undefined, false);
  assert.equal(r.status, 1); assert.match(r.stderr, /Build failed/);
  assert.deepEqual(snapshot(f.dst), before); load(f.dst, a, 'A'); assert.ok(!fs.existsSync(f.lock));
});
test('build runs under the lock and uses extension-local out', t => {
  const f = fixture(t);
  fs.cpSync(f.payload, path.join(f.extension, 'out'), {recursive: true});
  const file = path.join(f.extension, 'package.json'), manifest = JSON.parse(fs.readFileSync(file));
  manifest.scripts.build = `node -e "require('node:assert').ok(require('node:fs').existsSync('.place.lock'))"`;
  fs.writeFileSync(file, JSON.stringify(manifest));
  const r = f.run([], undefined, false); assert.equal(r.status, 0, r.stderr);
  load(f.dst, JSON.parse(fs.readFileSync(path.join(f.dst, 'package.json'))), 'A');
});
for (const contents of ['foreign-manifest', 'unknown-file']) test('refuses unowned ' + contents, t => {
  const f = fixture(t); fs.mkdirSync(f.dst, {recursive: true});
  fs.writeFileSync(path.join(f.dst, contents === 'foreign-manifest' ? 'package.json' : 'sentinel'), '{}');
  const before = snapshot(f.dst); assert.equal(f.run().status, 1); assert.deepEqual(snapshot(f.dst), before);
});
for (const where of ['workspace', '.vscode', '.vscode/extensions', '.vscode/extensions/loka-preview'])
  test('refuses symlink ' + where + ' without touching target', t => {
    const f = fixture(t), sentinel = path.join(f.root, 'sentinel'); fs.mkdirSync(sentinel);
    fs.writeFileSync(path.join(sentinel, 'keep'), 'unchanged');
    const link = where === 'workspace' ? path.join(f.root, 'alias') : path.join(f.workspace, where);
    fs.mkdirSync(path.dirname(link), {recursive: true}); fs.symlinkSync(sentinel, link);
    const before = snapshot(sentinel), mtime = fs.statSync(sentinel).mtimeMs;
    const r = f.run([], undefined, true, where === 'workspace' ? link : f.workspace);
    assert.equal(r.status, 1); assert.deepEqual(snapshot(sentinel), before);
    assert.equal(fs.statSync(sentinel).mtimeMs, mtime);
  });
for (const point of ['mid-copy', 'after-publish']) test('interrupted first placement recovers: ' + point, t => {
  const f = fixture(t); assert.equal(f.run([], point).status, 86);
  assert.ok(!fs.existsSync(path.join(f.dst, 'package.json'))); f.unlock();
  load(f.dst, f.place(), 'A');
});
test('clean retains only the current generation, removes stages, and keeps it loadable', t => {
  const f = fixture(t); f.place(); f.place(); const current = f.place();
  fs.mkdirSync(path.join(f.dst, '.stage-leftover'));
  const r = f.run(['--clean'], undefined, false); assert.equal(r.status, 0, r.stderr);
  assert.match(r.stdout, /all windows.*closed/);
  assert.deepEqual(fs.readdirSync(f.dst).sort(), [current.main.split('/')[1], 'package.json'].sort());
  load(f.dst, current, 'A');
});
test('clean refuses ambiguous current main; placement refuses payload symlinks', t => {
  const f = fixture(t); f.place();
  const file = path.join(f.dst, 'package.json'), manifest = JSON.parse(fs.readFileSync(file));
  manifest.main = './out/extension.js'; fs.writeFileSync(file, JSON.stringify(manifest));
  const before = snapshot(f.dst); assert.equal(f.run(['--clean'], undefined, false).status, 1);
  assert.deepEqual(snapshot(f.dst), before);
  fs.symlinkSync(path.join(f.payload, 'extension.js'), path.join(f.payload, 'link.js'));
  assert.equal(f.run().status, 1); assert.deepEqual(snapshot(f.dst), before);
});
test('final hint names destination, generation count and clean threshold', t => {
  const f = fixture(t); f.place();
  for (let i = 0; i < 20; i++) fs.mkdirSync(path.join(f.dst, 'gen-old-' + i));
  const r = f.run(); assert.equal(r.status, 0, r.stderr);
  const hint = r.stdout.trim().split('\n').at(-1);
  assert.ok(hint.includes(f.dst)); assert.match(hint, /22 generations.*Install Workspace Extension.*reopen.*place:clean/);
});
test('a missing workspace root is refused and not created', t => {
  const f = fixture(t);
  const missing = path.join(f.root, 'missing');
  const refused = f.run([], undefined, true, missing);
  assert.equal(refused.status, 1, refused.stdout + refused.stderr);
  assert.match(refused.stderr, /Workspace root does not exist/);
  assert.equal(fs.existsSync(missing), false);
  assert.equal(fs.existsSync(f.lock), false);
});
