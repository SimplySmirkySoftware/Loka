import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {join, resolve} from 'node:path';
import {capturePlan, containedPath, hostPage, resolveBuildPath} from '../driver/capture.mjs';

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
    promisify: () => () => {}, execFile: {},
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
      return {stdout: '<body data-loka-capture="ready">'};
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

test('browser cancellation and timeout kill the POSIX group before returning', async () => {
  for (const cause of ['abort', 'timeout']) {
    const controller = new AbortController(), kills = [];
    let timer, options, reject;
    const pending = new Promise((resolve, fail) => { reject = fail; });
    pending.child = {pid: 123, kill: () => assert.fail('must kill group')};
    const context = {
      URL, fileURLToPath: () => '/repo/', promisify: () => (file, args, opts) => { options = opts; return pending; }, execFile: {},
      process: {platform: 'linux', kill: (pid, signal) => { kills.push([pid, signal]); reject(new Error('killed')); }},
      setTimeout: (fn, ms) => { assert.equal(ms, 30000); timer = fn; return 1; },
      clearTimeout: id => { assert.equal(id, 1); timer = null; }, controller
    };
    const result = vm.runInNewContext(driverSource + "\nrunBrowser('browser', [], controller.signal);", context);
    if (cause === 'abort') controller.abort(); else timer();
    await assert.rejects(result, /killed/);
    assert.equal(options.detached, true);
    assert.deepEqual(kills, [[-123, 'SIGKILL']]);
    assert.equal(timer, null);
  }
});
