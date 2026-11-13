import test from 'node:test';
import {setImmediate} from 'node:timers/promises';
import assert from 'node:assert/strict';
import {mkdir, mkdtemp, readFile, rm, writeFile, symlink} from 'node:fs/promises';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {spawnSync} from 'node:child_process';
import {judge, statuses, prerequisiteDecision, validateInputs, pin, RunOwner, handshake,
  statusExpression, trustExpression, steps, stepStatuses, toastExpression, launchCells, repo, displayTaken} from './run.mjs';

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

const facts = {platform: 'linux', node: '24.19.0', python: true, xvfb: true, xauth: true, artifact: true};
const env = {LOKA_VSCODE: '/code', LOKA_WEB_BUILD: '/build'};
for (const [name, change, settings] of [
  ['Linux', {platform: 'darwin'}, {}], ['Node missing', {node: ''}, {}],
  ['Node 22.17', {node: '22.17.0'}, {}], ['Node 23', {node: '23.11.0'}, {}],
  ['Node unaudited patch', {node: '24.1.0'}, {}], ['Node unaudited major', {node: '25.0.0'}, {}],
  ['python3', {python: false}, {}], ['Xvfb', {xvfb: false}, {}], ['xauth', {xauth: false}, {}],
  ['code absent', {}, {LOKA_VSCODE: ''}], ['out absent', {artifact: false}, {}]
]) test('prerequisite optional/required: ' + name, () => {
  for (const require of ['', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME']) {
    const decision = prerequisiteDecision({...facts, ...change}, {...env, ...settings, [require]: '1'});
    assert.equal(decision.code, require ? 1 : 77);
    assert.match(decision.lines.join('\n'), /R1 R2 R3 R4 R5 T1 K3 K2 K1 T3 T1' T2/);
  }
});
test('runtime prerequisite and invalid input precedence', () => {
  for (const node of ['22.18.0', '24.0.0', '24.19.0']) {
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

// Fake files and CDP drive the production step loop, including each deadline origin.
function fixture(kind, options = {}) {
  const calls = [], acks = [];
  let now = 0, index = 0, observed = false;
  const originalNow = Date.now;
  Date.now = () => now;
  const owner = {
    abort: new AbortController(), cellDirectory: '/owned',
    code: {closed: options.pendingExit ? new Promise(() => {}) : Promise.resolve({code: options.exit ?? 0})},
    timers: new Set(),
    check() { this.abort.signal.throwIfAborted(); if (options.early) throw new Error('VS Code exited early'); },
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
      if (name === 'error.json') return (options.suiteError || options.errorAfterUI && observed) ? 'host failed' : undefined;
      if (options.missing) return undefined;
      now += 1000;
      const [cell, phase] = steps[kind][index];
      const facts = {...result(kind), finalTrusted: phase !== 'ready' && kind !== 'R',
        trustGranted: phase !== 'ready' && kind !== 'R',
        starts: ['K1', 'K2'].includes(cell) ? 1 : 0, executions: 0,
        exit: cell === 'K1' ? 0 : 3, ...options.patch};
      return options.raw ?? JSON.stringify({cell, phase, facts});
    },
    async go(n) { assert.equal(n, index++); acks.push(n); calls.push(['go', n]); }
  };
  const connect = async () => ({evaluate: async expression => {
    calls.push(['evaluate', expression]);
    observed = expression === (options.errorUI ?? statusExpression);
    if (options.cdpError) throw new Error('CDP failed');
    if (expression === trustExpression) return !options.noTrust;
    if (expression === toastExpression) return {text: 'last notification', picker: options.picker ?? false, toast: options.noToast ? false : steps[kind][index][1] === 'toast'};
    assert.equal(expression, statusExpression);
    return options.noStatus || options.noStatusAt === steps[kind][index][0] ? 'not ready' : stepStatuses[steps[kind][index][0]];
  }});
  return {calls, acks, owner, run: async () => {
    try { await handshake(owner, cell(kind), files, connect); } finally { Date.now = originalNow; }
  }};
}
test('steps: trust once, ordered acks, fresh UI and next-step deadlines, K1 control', async () => {
  const f = fixture('T3'); await f.run();
  assert.equal(f.calls.filter(c => c[1] === trustExpression).length, 1);
  assert.deepEqual(f.acks, [0, 1, 2, 3, 4, 5]);
  for (const [label, start, end] of f.calls.filter(c => ['status', 'Trust button'].includes(c[0])))
    assert.equal(end - start, 10000, label);
  assert.deepEqual(f.calls.filter(c => /^step-/.test(c[0])).map(c => c[2] - c[1]),
    [40000, 15000, 15000, 45000, 45000, 15000]);
});
test('steps: Restricted Mode never clicks trust; T2 uses the same mechanism', async () => {
  const r = fixture('R'); await r.run();
  assert.ok(!r.calls.some(c => c[1] === trustExpression));
  await fixture('T2').run();
});
for (const [name, options, message] of [
  ['missing step', {missing: true}, /step-0 timed out/],
  ['malformed step', {raw: '{'}, /Malformed/],
  ['error wins over valid step', {suiteError: true}, /host failed/],
  ['wrong folder', {patch: {folders: ['/wrong']}}, /Workspace folder/],
  ['wrong sequence', {raw: JSON.stringify({cell: 'K1', phase: 'ready', facts: {}})}, /identity/],
  ['missing status selector', {noStatus: true}, /status timed out/],
  ['no trust click', {noTrust: true}, /Trust button timed out/],
  ['error during observation', {errorAfterUI: true, noStatus: true}, /host failed/],
  ['CDP failure', {cdpError: true}, /CDP failed/],
  ['early exit', {early: true}, /exited early/],
  ['nonzero exit', {exit: 9}, /Exit:/]
]) test('step failure: ' + name, async () => {
  const f = fixture('T3', options);
  await assert.rejects(f.run(), message);
  assert.equal(f.owner.abort.signal.aborted, true);
  assert.equal(f.acks.length, options.exit === 9 ? 6 : options.noStatus ? 2 : 0);
});
test('step failure: error during unsatisfied toast observation', async () => {
  const f = fixture('R', {errorAfterUI: true, errorUI: toastExpression, picker: true});
  await assert.rejects(f.run(), /host failed/);
  assert.deepEqual(f.acks, [0, 1, 2]);
});
test('steps: exit deadline is 15 seconds after final go', async t => {
  t.mock.timers.enable({apis: ['setTimeout']});
  const f = fixture('R', {pendingExit: true});
  let failure;
  const running = f.run().catch(error => { failure = error; });
  await setImmediate();
  assert.equal(f.acks.length, 6);
  t.mock.timers.tick(14999); await setImmediate(); assert.equal(failure, undefined);
  t.mock.timers.tick(1); await setImmediate();
  assert.match(failure?.message ?? '', /VS Code exit timed out/);
  await running;
});
test('status selector string contract names exactly the pinned item', () => {
  assert.equal(statusExpression, "document.getElementById('simplysmirkysoftware.loka-preview')?.innerText.trim() ?? ''");
});
test('owner spawn failure still removes its directory', async () => {
  const root = join(repo, 'build/vscode-smoke'); await mkdir(root, {recursive: true});
  const directory = await mkdtemp(join(root, 'test-spawn-'));
  const owner = new RunOwner(directory, {signal: async () => false});
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
    for (const version of [null, 'v22.17.0', 'v23.0.0', 'broken', 'v24.0.0']) {
      if (version) await writeFile(join(directory, 'node'), '#!/bin/bash\necho ' + version + '\n', {mode: 0o700});
      for (const required of ['', 'LOKA_REQUIRE_VSCODE_SMOKE', 'LOKA_REQUIRE_VSCODE_RUNTIME']) {
        const run = spawnSync('/bin/bash', [join(repo, 'tests/scripts/VscodeLokaSmokeTest.sh')],
          {env: {PATH: directory, [required]: '1'}, encoding: 'utf8'});
        assert.equal(run.status, required ? 1 : 77, run.stdout + run.stderr);
        assert.match(run.stdout, /R1 R2 R3 R4 R5 T1 K3 K2 K1 T3 T1' T2/);
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

test('launch order and failed positive control never admit Restricted Mode', async () => {
  const launches = [], reports = [];
  await launchCells(true, async kind => launches.push(kind));
  assert.deepEqual(launches, ['T3', 'R', 'T2']);
  launches.length = 0;
  await assert.rejects(launchCells(true, async kind => { launches.push(kind); throw new Error('K1 failed'); },
    line => reports.push(line)), /K1 failed/);
  assert.deepEqual(launches, ['T3']);
  assert.match(reports[0], /not run.*control K1 not established/);
});

test('K3 does not print a pass when K2 status fails', async () => {
  const logs = [], original = console.log;
  console.log = line => logs.push(line);
  try { await assert.rejects(fixture('T3', {noStatusAt: 'K2'}).run(), /status timed out/); }
  finally { console.log = original; }
  assert.ok(!logs.some(line => line.includes('[pass] K3')));
});
test('R4 cannot reuse R3 text across intervening Open', async () => {
  const f = fixture('R', {noStatusAt: 'R4'});
  await assert.rejects(f.run(), /status timed out/);
  assert.deepEqual(f.acks, [0, 1, 2]);
});

test('unaudited Node prerequisite gives the exact reason', () => {
  for (const required of [false, true]) {
    const decision = prerequisiteDecision({...facts, node: '25.0.0'},
      {...env, LOKA_REQUIRE_VSCODE_SMOKE: required ? '1' : '0'});
    assert.equal(decision.code, required ? 1 : 77);
    assert.equal(decision.lines[0], `[${required ? 'fail' : 'skip'}] R1 R2 R3 R4 R5 T1 K3 K2 K1 T3 T1' T2: Node 25.0.0 is not in the audited list for child-signal ordering`);
  }
});
test('display selection skips live locks and admits stale or absent locks', async () => {
  for (const pid of [NaN, 0, -1, 123, 456]) {
    const calls = [];
    assert.equal(await displayTaken(99, async display => {
      assert.equal(display, 99); return pid;
    }, value => { calls.push(value); return value === 123; }), pid === 123);
    assert.deepEqual(calls, pid > 0 ? [pid] : []);
  }
});
test('cleanup checks the owned X lock after close, allowing absent or retaken locks', async () => {
  for (const lockPid of [NaN, 314, 999]) {
    let closed = false;
    const directory = await mkdtemp(join(tmpdir(), 'loka-lock-test-'));
    const owner = new RunOwner(directory, {signal: async () => false, readLock: async display => {
      assert.equal(closed, true); assert.equal(display, 99); return lockPid;
    }});
    owner.x = {display: 99, child: {pid: 314}, closed: Promise.resolve().then(() => { closed = true; })};
    if (lockPid === 314) await assert.rejects(owner.cleanup(), /Xvfb exited leaving its own lock/);
    else await owner.cleanup();
    await assert.rejects(readFile(directory), /ENOENT/);
  }
});
test('wait failures carry cell, phase, expected text and last read text', async () => {
  await assert.rejects(fixture('T3', {noStatusAt: 'K2'}).run(),
    /K2\/observed: status timed out; expected "Loka: Build failed: 3", last read "not ready"/);
  await assert.rejects(fixture('R', {noToast: true}).run(),
    /R5\/toast: toast\/picker timed out; expected "Trust the workspace to select an example" present and picker absent, last read "last notification"/);
  await assert.rejects(fixture('T3', {missing: true}).run(), /T1\/ready: step-0 timed out/);
});
