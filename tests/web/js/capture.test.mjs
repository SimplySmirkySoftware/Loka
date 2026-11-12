import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {setTimeout as delay} from 'node:timers/promises';
import {join, resolve} from 'node:path';
import {capturePlan, containedPath, hostPage, resolveBuildPath, runBrowser} from '../driver/capture.mjs';

const apps = [
  {id: 'First', exportName: 'LokaFirst', js: 'example/with space/first.js'},
  {id: 'Second', exportName: 'LokaSecond', js: 'example/日本語/second.js'}
];
const catalog = {version: 1, apps};
const skins = JSON.parse(await readFile(new URL('../../../tools/style-vocab.json', import.meta.url), 'utf8')).webSkins;

test('every catalog identity and vocabulary skin gets a distinct capture', () => {
  const plan = capturePlan(catalog, skins, 'http://127.0.0.1:1234');
  assert.equal(plan.length, apps.length * skins.length);
  assert.equal(new Set(plan.map(item => item.outputName)).size, plan.length, 'app id prevents output collisions');
  for (const app of apps) for (const {name: skin} of skins) {
    const item = plan.find(item => item.app === app && item.skin === skin);
    assert.ok(item);
    const url = new URL(item.url);
    assert.equal(url.origin, 'http://127.0.0.1:1234');
    assert.equal(url.pathname, '/__loka_capture.html');
    assert.equal(url.searchParams.get('app'), app.id);
    assert.equal(url.searchParams.get('skin'), skin, 'skin travels through the URL');
    assert.equal(item.outputName, app.id + '-' + skin + '.png');
  }
  assert.deepEqual(capturePlan({version: 1, apps: []}, skins, ''), []);
  assert.throws(() => capturePlan({...catalog, version: 2}, skins, ''), /catalog/);
  assert.throws(() => capturePlan({...catalog, apps: [apps[0], apps[0]]}, skins, ''), /catalog/);
  for (const patch of [{id: '../outside'}, {exportName: 'bad()'}, {js: '../outside.js'}, {js: '/outside.js'}])
    assert.throws(() => capturePlan({...catalog, apps: [{...apps[0], ...patch}]}, skins, ''), /catalog/);
  assert.throws(() => capturePlan(catalog, [{name: '../bad'}], ''), /webSkins/);
});

test('server resolution refuses traversal and sibling-prefix escapes', () => {
  const root = resolve('build/test-capture-root');
  assert.equal(resolveBuildPath(root, '/example/with%20space/app.js?v=1'), resolve(root, 'example/with space/app.js'));
  for (const path of ['/../secret', '/%2e%2e/secret', '/a/../../secret', '/a/%2e%2e/secret',
    '/%2e%2e%2fsecret', '/..%5csecret', '/%00', '/%zz', 'relative.js', '/'])
    assert.equal(resolveBuildPath(root, path), null, 'refuse ' + path);
  assert.equal(containedPath(root, root + '-sibling/file'), false);
  assert.equal(containedPath(root, resolve(root, '..', 'secret')), false);
  assert.equal(containedPath(root, resolve(root, 'app.wasm')), true);
});

async function boot(result, rejectFactory = false, failScript = false, deferred = null) {
  const attrs = {}, calls = [], listeners = {};
  const html = hostPage(apps[0], 'classic');
  const context = {
    document: {
      createElement: tag => { assert.equal(tag, 'script'); return {}; },
      head: {appendChild: script => { calls.push(script.src); failScript ? script.onerror() : script.onload(); }},
      body: {setAttribute: (key, value) => { attrs[key] = value; }},
      fonts: {ready: {then: done => {
        assert.ok(calls.includes('start'));
        if (deferred) listeners[deferred]({error: 'Deferred error', reason: 'Deferred rejection'});
        done();
      }}}
    },
    window: {addEventListener: (name, callback) => { listeners[name] = callback; }, LokaFirst: async options => {
      calls.push(options.lokaSkin);
      if (rejectFactory) throw new Error('Factory refused');
      return {web: {start: () => { calls.push('start'); return result; }}};
    }}
  };
  await vm.runInNewContext(html.match(/<script>([\s\S]*)<\/script>/)[1], context);
  return {attrs, calls, html, listeners};
}

test('host loads the catalog factory, passes the skin, starts, and marks readiness without visible chrome', async () => {
  const {attrs, calls, html} = await boot(0);
  assert.deepEqual(calls, ['/example/with%20space/first.js', 'classic', 'start']);
  assert.equal(attrs['data-loka-capture'], 'ready');
  assert.match(html, /margin:0;overflow:hidden/);
  assert.match(html, /scrollbar-width:none/);
  assert.match(html, /<body><script>/);
  const escaped = hostPage({...apps[0], js: 'example/</script>.js'}, 'classic');
  assert.equal((escaped.match(/<\/script>/g) || []).length, 1);
});

test('failed starts, rejected factories and script loads never mark readiness', async () => {
  for (const [args, message] of [[[2], /web.start\(\) failed: 2/], [[0, true], /Factory refused/], [[0, false, true], /script failed/]]) {
    const {attrs} = await boot(...args);
    assert.equal(attrs['data-loka-capture'], 'failed');
    assert.match(attrs['data-loka-error'], message);
  }
});


test('deferred errors and rejections irreversibly invalidate readiness', async () => {
  for (const event of ['error', 'unhandledrejection']) {
    const during = await boot(0, false, false, event);
    assert.equal(during.attrs['data-loka-capture'], 'failed', 'fonts completion cannot restore ready');
    const after = await boot(0);
    after.listeners[event]({error: 'Late error', reason: 'Late rejection'});
    assert.equal(after.attrs['data-loka-capture'], 'failed', 'failure after ready invalidates it');
    assert.match(after.attrs['data-loka-error'], /Late/);
  }
});

// Execute the real entry/cleanup code with IO stubs: no socket or browser needed.
const driverSource = (await readFile(new URL('../driver/capture.mjs', import.meta.url), 'utf8'))
  .replace(/^#!.*\n/, '').replace(/^import .*;\n/gm, '')
  .replace(/^export /gm, '').replace(/import\.meta\.url/g, "'file:///repo/tests/web/driver/capture.mjs'")
  .split('\nif (process.argv[1]')[0];

async function invocation(failure, cancelAt) {
  const events = [], signals = {};
  let images = ['old.png'];
  const context = {
    join, resolve, fileURLToPath: () => '/repo/', AbortController, URL, URLSearchParams,
    process: {argv: ['node', 'capture'], env: failure === 'chromium' ? {} : {CHROMIUM: 'browser'},
      on: (name, fn) => { signals[name] = fn; }, off: name => { delete signals[name]; }},
    spawnSync: () => ({status: 1}),
    mkdir: async path => { events.push('mkdir:' + path); if (path.endsWith('web-captures') && images === null) images = []; },
    realpath: async path => path, lstat: async () => ({isSymbolicLink: () => failure === 'symlink'}),
    rm: async path => { events.push('rm:' + path); if (path === '/repo/build/web-captures') images = null; },
    readFile: async path => {
      if (path.endsWith('loka-web-apps.json')) {
        assert.equal(images, null, 'old batch removed before catalog read');
        if (failure === 'catalog') throw new Error('missing catalog');
        return JSON.stringify(catalog);
      }
      return JSON.stringify({webSkins: [{name: 'web'}]});
    },
    createServer: () => ({once() {}, listen: (port, host, done) => done(),
      address: () => ({port: 1234}), closeAllConnections() {},
      close: done => { if (cancelAt === 'close') signals.SIGTERM(); done(); }}),
    mkdtemp: async path => path + 'temp', tmpdir: () => '/tmp',
    stat: async () => { if (cancelAt === 'stat') signals.SIGTERM(); return {isFile: () => true, size: 1}; },
    rename: async (from, to) => { events.push('promote'); images.push(to); },
    console: {log() {}},
    browser: async () => {
      if (failure === 'browser' || (failure === 'partial' && images.length)) throw new Error('browser failed');
      return '<body data-loka-capture="ready">';
    }
  };
  let error;
  try { await vm.runInNewContext(driverSource + '\nrunBrowser = browser; main();', context); }
  catch (caught) { error = caught; }
  return {error, images, events, signals};
}

test('batch invalidation precedes input errors and discards partial batches', async () => {
  const success = await invocation();
  assert.equal(success.error, undefined);
  assert.equal(success.images.length, 2);
  assert.ok(success.events.indexOf('rm:/repo/build/web-captures') < success.events.indexOf('mkdir:/repo/build/web-captures'));
  for (const failure of ['catalog', 'chromium', 'browser', 'partial']) {
    const result = await invocation(failure);
    assert.ok(result.error);
    assert.equal(result.images, null, failure + ' leaves no batch');
    assert.deepEqual(Object.keys(result.signals), []);
  }
  const refused = await invocation('symlink');
  assert.match(refused.error.message, /symlink/);
  assert.deepEqual(refused.images, ['old.png']);
  assert.ok(!refused.events.some(event => event.startsWith('rm:')));
});

test('cancellation during validation or server close cannot succeed', async () => {
  for (const timing of ['stat', 'close']) {
    const result = await invocation(undefined, timing);
    assert.match(result.error?.message || '', /interrupted/);
    assert.equal(result.images, null);
    if (timing === 'stat') assert.ok(!result.events.includes('promote'));
  }
});

// A bounded watchdog also cleans up deliberately broken mutation runs.
async function poll(check, message, ms = 3000) {
  const deadline = Date.now() + ms;
  while (!await check()) {
    assert.ok(Date.now() < deadline, message);
    await delay(20);
  }
}

function gone(pid) {
  try { process.kill(pid, 0); return false; }
  catch (error) { if (error.code === 'ESRCH') return true; throw error; }
}

function kill(pid) {
  try { process.kill(pid, 'SIGKILL'); }
  catch (error) { if (error.code !== 'ESRCH') throw error; }
}

if (process.platform === 'win32') console.log('[skip] real browser process tests: POSIX process groups required; Windows descendants unsupported');

for (const cause of ['abort', 'timeout']) test('real browser ' + cause + ' waits for launcher and kills descendant',
  {skip: process.platform === 'win32'}, async () => {
    const dir = await mkdtemp(join(tmpdir(), 'loka-capture-test-'));
    const launcher = join(dir, 'launcher.sh'), pidFile = join(dir, 'pids');
    const controller = new AbortController();
    let pids = [], watchdog, outcome;
    try {
      await writeFile(launcher, 'sleep 60 &\nchild=$!\nprintf "%s %s\\n" "$$" "$child" | tee "$1"\nwait "$child"\n');
      const pending = runBrowser('/bin/sh', [launcher, pidFile], controller.signal, cause === 'timeout' ? 1000 : 30000);
      // Observe rejection immediately, including failures before the fixture is ready.
      outcome = pending.then(stdout => ({stdout}), error => ({error}));
      await poll(async () => {
        pids = (await readFile(pidFile, 'utf8').catch(() => '')).trim().split(/\s+/).map(Number);
        return pids.length === 2 && pids.every(pid => Number.isInteger(pid) && pid > 0);
      }, 'launcher did not publish both PIDs');
      const deadline = new Promise(resolve => {
        watchdog = setTimeout(() => resolve({error: new Error('test watchdog expired')}), 4000);
      });
      if (cause === 'abort') controller.abort(new Error('test abort'));
      const result = await Promise.race([outcome, deadline]);
      assert.match(result.error?.message || '', cause === 'abort' ? /test abort/ : /timed out/);
      assert.equal(gone(pids[0]), true, 'promise settles after launcher close');
      await poll(() => pids.every(gone), 'launcher and descendant must both reach ESRCH');
    } finally {
      clearTimeout(watchdog);
      for (const pid of pids.filter(pid => Number.isInteger(pid) && pid > 0)) kill(pid);
      await outcome;
      await rm(dir, {recursive: true, force: true});
    }
  });

test('real browser exit, output bound, spawn error and pre-abort', {skip: process.platform === 'win32'}, async () => {
  const controller = new AbortController();
  assert.equal(await runBrowser('/bin/sh', ['-c', 'printf hello'], controller.signal), 'hello');
  await assert.rejects(runBrowser('/bin/sh', ['-c', 'exit 7'], controller.signal), /exited with 7/);
  await assert.rejects(runBrowser('/bin/sh', ['-c', 'exec dd if=/dev/zero bs=1048576 count=9'], controller.signal), /stdout exceeded/);
  const dir = await mkdtemp(join(tmpdir(), 'loka-capture-missing-'));
  try {
    await assert.rejects(runBrowser(join(dir, 'missing'), [], controller.signal), /Chromium missing:/);
    controller.abort(new Error('already aborted'));
    await assert.rejects(runBrowser(process.execPath, [], controller.signal), /already aborted/);
  } finally { await rm(dir, {recursive: true, force: true}); }
});
