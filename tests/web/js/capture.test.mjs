import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {resolve} from 'node:path';
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

async function boot(result, rejectFactory = false, failScript = false) {
  const attrs = {}, calls = [];
  const html = hostPage(apps[0], 'classic');
  const context = {
    document: {
      createElement: tag => { assert.equal(tag, 'script'); return {}; },
      head: {appendChild: script => { calls.push(script.src); failScript ? script.onerror() : script.onload(); }},
      body: {setAttribute: (key, value) => { attrs[key] = value; }},
      fonts: {ready: Promise.resolve()}
    },
    window: {LokaFirst: async options => {
      calls.push(options.lokaSkin);
      if (rejectFactory) throw new Error('Factory refused');
      return {web: {start: () => { calls.push('start'); return result; }}};
    }}
  };
  await vm.runInNewContext(html.match(/<script>([\s\S]*)<\/script>/)[1], context);
  return {attrs, calls, html};
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
