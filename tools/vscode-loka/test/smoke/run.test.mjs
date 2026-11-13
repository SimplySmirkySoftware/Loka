import test from 'node:test';
import {setImmediate} from 'node:timers/promises';
import assert from 'node:assert/strict';
import {mkdir, mkdtemp, readFile, rm, writeFile, symlink} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {judge, statuses, prerequisiteDecision, validateInputs, pin, RunOwner, handshake,
  statusExpression, trustExpression, rollCall, repo} from './run.mjs';

const manifest = JSON.parse(await readFile(new URL('../../package.json', import.meta.url), 'utf8'));
const cell = kind => ({kind, workspace: '/owned/ws', buildDirectory: '/owned/build', started: 0});
const result = kind => ({active: true, commands: manifest.contributes.commands.map(c => c.command),
  trustEnabled: true, initialTrusted: false, finalTrusted: kind !== 'R', trustGranted: kind !== 'R',
  folders: ['/owned/ws'], buildDirectory: '/owned/build', tabs: ['Loka Preview']});

for (const kind of Object.keys(statuses)) {
  test('judge ' + kind + ': success and discriminating failures', () => {
    assert.deepEqual(judge(cell(kind), result(kind), statuses[kind], 0), []);
    for (const patch of [{active: false}, {commands: []}, {initialTrusted: true}, {trustEnabled: false},
      {finalTrusted: kind === 'R'}, {trustGranted: kind === 'R'}, {folders: ['/wrong']},
      {folders: ['/owned/ws', '/other']}, {buildDirectory: '/wrong'}, {tabs: []},
      {tabs: ['Loka Preview', 'Loka Preview']}, {error: 'activation threw'}]) {
      assert.ok(judge(cell(kind), {...result(kind), ...patch}, statuses[kind], 0).length, JSON.stringify(patch));
    }
    assert.ok(judge(cell(kind), result(kind), 'Loka: Windowless', 0).length);
    assert.ok(judge(cell(kind), result(kind), 'wrong', 0).length);
    for (const exit of [undefined, null, 1, 'early exit']) assert.ok(judge(cell(kind), result(kind), statuses[kind], exit).length);
    for (const missing of [undefined, null, '{broken', [], {}]) assert.ok(judge(cell(kind), missing, statuses[kind], 0).length);
    for (const command of result(kind).commands) {
      assert.ok(judge(cell(kind), {...result(kind), commands: result(kind).commands.filter(c => c !== command)}, statuses[kind], 0).length);
    }
  });
}

const facts = {platform: 'linux', node: '24.19.0', xvfb: true, xauth: true, artifact: true};
const env = {LOKA_VSCODE: '/code', LOKA_WEB_BUILD: '/build'};
for (const [name, change, settings] of [
  ['Linux', {platform: 'darwin'}, {}], ['Node missing', {node: ''}, {}],
  ['Node 22.17', {node: '22.17.0'}, {}], ['Node 23', {node: '23.11.0'}, {}],
  ['Xvfb', {xvfb: false}, {}], ['xauth', {xauth: false}, {}],
  ['code absent', {}, {LOKA_VSCODE: ''}], ['out absent', {artifact: false}, {}]
]) test('prerequisite optional/required: ' + name, () => {
  for (const require of ['', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME']) {
    const decision = prerequisiteDecision({...facts, ...change}, {...env, ...settings, [require]: '1'});
    assert.equal(decision.code, require ? 1 : 77);
    assert.match(decision.lines.join('\n'), /R1 R2 T1 T3 T1' T2/);
  }
});
test('runtime prerequisite and invalid input precedence', () => {
  for (const node of ['22.18.0', '24.0.0', '25.0.0']) {
    assert.equal(prerequisiteDecision({...facts, node}, env).code, 0);
  }
  for (const required of ['', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME']) {
    const decision = prerequisiteDecision(facts, {...env, LOKA_WEB_BUILD: '', [required]: '1'});
    assert.equal(decision.code, required.endsWith('RUNTIME') ? 1 : 0);
    assert.match(decision.lines.join('\n'), /T1' T2: LOKA_WEB_BUILD not set/);
  }
  assert.equal(prerequisiteDecision({...facts, xvfb: false, invalid: 'bad pin'}, {}).code, 1);
});
test('supplied code: missing, non-executable, wrapper, wrong pin, symlink; invalid Web tree', async () => {
  const root = await mkdtemp(join(tmpdir(), 'loka-smoke-input-'));
  try {
    const code = join(root, 'code');
    await assert.rejects(validateInputs({LOKA_VSCODE: code}), /ENOENT/);
    await writeFile(code, 'code', {mode: 0o600});
    await assert.rejects(validateInputs({LOKA_VSCODE: code}), /EACCES/);
    await rm(code); await writeFile(code, 'code', {mode: 0o700});
    await assert.rejects(validateInputs({LOKA_VSCODE: code}), /ENOENT/);
    await mkdir(join(root, 'resources/app'), {recursive: true});
    await writeFile(join(root, 'resources/app/product.json'), JSON.stringify({...pin, commit: 'wrong'}));
    await assert.rejects(validateInputs({LOKA_VSCODE: code}), /pin mismatch/);
    await writeFile(join(root, 'resources/app/product.json'), JSON.stringify(pin));
    const link = join(root, 'alias'); await symlink(code, link);
    assert.equal(await validateInputs({LOKA_VSCODE: link}), code);
    await mkdir(join(root, 'bin')); await writeFile(join(root, 'bin/code'), 'wrapper', {mode: 0o700});
    await assert.rejects(validateInputs({LOKA_VSCODE: join(root, 'bin/code')}), /ENOENT/);
    await assert.rejects(validateInputs({LOKA_WEB_BUILD: code}), /absolute directory/);
    await assert.rejects(validateInputs({LOKA_WEB_BUILD: 'relative'}), /absolute directory/);
  } finally { await rm(root, {recursive: true, force: true}); }
});

// Virtual clock runs the exported handshake itself, including checks around every awaited read.
function fixture(kind, options = {}) {
  const calls = [];
  let now = 0, released = false;
  const originalNow = Date.now;
  Date.now = () => now;
  const owner = {
    cellDirectory: '/owned',
    code: {closed: options.pendingExit ? new Promise(() => {}) : Promise.resolve({code: options.exit ?? 0})},
    timers: new Set(),
    check() { if (options.early) throw new Error('VS Code exited early'); },
    limit: options.pendingExit ? RunOwner.prototype.limit : async promise => promise,
    async wait(read, deadline, label) {
      calls.push([label, now, deadline]);
      for (;;) {
        this.check();
        const value = await read();
        this.check();
        if (value) return value;
        if (now >= deadline) throw new Error(label + ' timed out');
        now += 1000;
      }
    }
  };
  const files = {
    async read(name) {
      if (name === 'ready') return true;
      if (options.missing) return undefined;
      now = 39000; // status gets its own ten seconds, not the startup remainder.
      return options.raw ?? JSON.stringify(result(kind));
    },
    async release() { released = true; calls.push(['release']); }
  };
  const connect = async () => ({evaluate: async expression => {
    calls.push(['evaluate', expression]);
    assert.equal(released, false);
    if (options.cdpError) throw new Error('CDP failed');
    if (expression === trustExpression) return !options.noTrust;
    assert.equal(expression, statusExpression);
    return options.noStatus ? 'not ready' : statuses[kind];
  }});
  return {calls, released: () => released, run: async () => {
    try { await handshake(owner, cell(kind), files, connect); } finally { Date.now = originalNow; }
  }};
}
test('handshake: ready, one trust click, result, full status deadline, release, exit', async () => {
  const f = fixture('T3'); await f.run();
  assert.equal(f.calls.filter(c => c[1] === trustExpression).length, 1);
  assert.deepEqual(f.calls.find(c => c[0] === 'status'), ['status', 39000, 49000]);
  assert.ok(f.calls.findIndex(c => c[0] === 'ready') < f.calls.findIndex(c => c[1] === trustExpression));
  assert.equal(f.calls.at(-1)[0], 'release');
});
test('handshake: restricted mode never clicks trust', async () => {
  const f = fixture('R'); await f.run();
  assert.ok(!f.calls.some(c => c[1] === trustExpression));
});
for (const [name, options, message] of [
  ['missing result', {missing: true}, /result.json timed out/],
  ['malformed result', {raw: '{'}, /Malformed/],
  ['suite error', {raw: JSON.stringify({error: 'host failed'})}, /host failed/],
  ['wrong folder', {raw: JSON.stringify({...result('T3'), folders: ['/wrong']})}, /Workspace folder/],
  ['missing status selector', {noStatus: true}, /status timed out/],
  ['no trust click', {noTrust: true}, /Trust button timed out/],
  ['CDP failure', {cdpError: true}, /CDP failed/],
  ['early exit', {early: true}, /exited early/],
  ['nonzero exit', {exit: 9}, /Exit: 9/]
]) test('handshake failure: ' + name, async () => {
  const f = fixture('T3', options);
  await assert.rejects(f.run(), message);
  assert.equal(f.released(), options.exit === 9);
});
test('handshake: pending exit fails at 15 seconds after release', async t => {
  t.mock.timers.enable({apis: ['setTimeout']});
  const f = fixture('R', {pendingExit: true});
  let failure;
  const running = f.run().catch(error => { failure = error; });
  await setImmediate();
  assert.equal(f.released(), true);
  t.mock.timers.tick(14999);
  await setImmediate();
  assert.equal(failure, undefined);
  t.mock.timers.tick(1);
  await setImmediate();
  assert.match(failure?.message ?? '', /VS Code exit timed out/);
  await running;
});
test('status selector string contract names exactly the pinned item', () => {
  assert.equal(statusExpression, "document.getElementById('simplysmirkysoftware.loka-preview')?.innerText.trim() ?? ''");
});
test('roll call reports group and escaped run marker, excludes own X; read errors fail closed', async () => {
  const fs = {readdir: async () => ['1', '2', '3', '4', 'self'], readFile: async path => {
    const pid = Number(path.split('/')[2]);
    return path.endsWith('stat') ? `${pid} (fake process) S 1 ${pid === 1 ? 10 : 20} 0` : (pid !== 4 ? '/owned/run\0' : 'other');
  }};
  assert.deepEqual((await rollCall(10, '/owned/run', 3, fs)).map(r => r.pid), [1, 2]);
  for (const code of ['EACCES', 'EIO']) {
    await assert.rejects(rollCall(10, '/owned/run', 3, {...fs, readFile: async () => { throw Object.assign(new Error(code), {code}); }}), new RegExp(code));
  }
  assert.deepEqual(await rollCall(10, '/owned/run', 3, {...fs, readFile: async () => { throw Object.assign(new Error('gone'), {code: 'ENOENT'}); }}), []);
});
test('owner spawn failure still removes its directory', async () => {
  const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
  const directory = await mkdtemp(join(root, 'test-spawn-'));
  const owner = new RunOwner(directory);
  owner.startCode(join(directory, 'absent'), [], process.env);
  await owner.code.closed;
  assert.throws(() => owner.check(), /ENOENT/);
  await owner.cleanup();
  await assert.rejects(readFile(directory), /ENOENT/);
  assert.equal(owner.timers.size, 0);
});
test('shell Node bootstrap optional and required branches', async () => {
  const directory = await mkdtemp(join(tmpdir(), 'loka-smoke-shell-'));
  try {
    for (const version of [null, 'v22.17.0', 'v23.0.0', 'broken']) {
      if (version) await writeFile(join(directory, 'node'), '#!/bin/bash\necho ' + version + '\n', {mode: 0o700});
      for (const required of ['', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME']) {
        const run = spawnSync('/bin/bash', [join(repo, 'tests/scripts/VscodeLokaSmokeTest.sh')],
          {env: {PATH: directory, [required]: '1'}, encoding: 'utf8'});
        assert.equal(run.status, required ? 1 : 77, run.stdout + run.stderr);
        assert.match(run.stdout, /R1 R2 T1 T3 T1' T2/);
      }
    }
  } finally { await rm(directory, {recursive: true, force: true}); }
});
test('owner waits bound a stalled adapter and detect cancellation/early close after a read', async () => {
  const owner = new RunOwner('/unused');
  await assert.rejects(owner.wait(() => new Promise(() => {}), Date.now() + 10, 'stalled CDP'), /stalled CDP timed out/);
  assert.equal(owner.timers.size, 0);
  await assert.rejects(owner.wait(() => true, Date.now() - 1, 'late result'), /late result timed out/);
  owner.abort.abort(new Error('test SIGTERM'));
  await assert.rejects(owner.wait(() => true, Date.now() + 1000, 'read'), /test SIGTERM/);
  const exited = new RunOwner('/unused');
  await assert.rejects(exited.wait(() => {
    exited.code = {outcome: {code: 0}};
    return 'result already written';
  }, Date.now() + 1000, 'read'), /exited early/);
});
