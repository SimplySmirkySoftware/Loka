import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeCatalog, readCatalog, currentExample} from '../src/catalog.ts';
import type {App} from '../src/catalog.ts';

const app: App = {id: 'Target', target: 'Target', name: 'Display 名; label', exportName: 'Factory',
  sourceDir: 'example/Display 名; label', js: 'nested/Payload.js'};
const decode = (apps: unknown = [app], version: unknown = 1) => decodeCatalog(JSON.stringify({version, apps}));
test('current example table preserves producer order and explicit identity', () => {
  const other = {...app, id: 'Second', target: 'Second'};
  const c = decode([app, other]); assert.equal(c.kind, 'catalog'); if (c.kind !== 'catalog') return;
  assert.equal(currentExample(c, undefined), c.apps[0]);
  assert.equal(currentExample(c, ''), c.apps[0]);
  assert.equal(currentExample(c, 'Second'), c.apps[1]);
  assert.equal('message' in currentExample(c, 'removed'), true);
  assert.deepEqual(currentExample({kind: 'catalog', apps: []}, ''), {kind: 'empty', message: 'Web catalog empty; run Configure'});
  const reversed = {kind: 'catalog' as const, apps: [other, app]};
  assert.equal(currentExample(reversed, ''), other);
  assert.equal(currentExample(reversed, 'Target'), app);
});
test('catalog diagnostics distinguish missing unreadable malformed unsupported and empty', () => {
  assert.equal(readCatalog('catalog', () => { throw {code: 'ENOENT'}; }).kind, 'missing');
  for (const error of [{code: 'EACCES'}, {code: 'EISDIR'}, null]) {
    assert.equal(readCatalog('catalog', () => { throw error; }).kind, 'unreadable');
  }
  assert.equal(readCatalog('catalog', () => '{').kind, 'malformed');
  assert.equal(decode([], 2).kind, 'unsupported');
  assert.equal(decode([]).kind, 'empty');
  for (const value of ['null', '[]', '{}', '1', '{"version":"1","apps":[]}', '{"version":1,"apps":{}}']) {
    assert.equal(decodeCatalog(value).kind, 'malformed', value);
  }
});
for (const field of ['id', 'name', 'target', 'exportName', 'sourceDir', 'js']) {
  test('catalog field type ' + field, () => {
    for (const value of [null, 1, {}, []]) assert.equal(decode([{...app, [field]: value}]).kind, 'malformed');
  });
}
for (const [name, rows] of Object.entries({
  'row object': [null], 'row array': [[]],
  'target name': [{...app, id: 'bad-target', target: 'bad-target'}],
  'target trailing newline': [{...app, id: 'Target\n', target: 'Target\n'}],
  'target empty': [{...app, id: '', target: ''}],
  'export trailing newline': [{...app, exportName: 'Factory\n'}],
  'export name': [{...app, exportName: '1Factory'}],
  'identity equality': [{...app, id: 'Other'}],
  'identity uniqueness': [app, app],
  'source base': [{...app, sourceDir: 'elsewhere/Label'}],
  'source child': [{...app, sourceDir: 'example/'}],
  'js suffix': [{...app, js: 'Payload.wasm'}],
})) test('catalog guard ' + name, () => assert.equal(decode(rows).kind, 'malformed'));
for (const field of ['sourceDir', 'js']) {
  for (const path of ['', '/absolute.js', '~home/file.js', 'C:relative.js', 'C:/absolute.js',
    'example/../Payload.js', 'example/..', '../Payload.js', 'example\\Payload.js', 'example/$<CONFIG>/Payload.js']) {
    test(`catalog path ${field} ${JSON.stringify(path)}`, () => assert.equal(decode([{...app, [field]: path}]).kind, 'malformed'));
  }
}
test('catalog accepts lexical dot components, whitespace, Unicode and semicolons without sorting', () => {
  assert.equal(decode([{...app, sourceDir: 'example/\nlabel', js: './nested//名 ;/Payload.js'}]).kind, 'catalog');
});

test('unknown fields cannot masquerade as a diagnostic in a completed row', () => {
  const c = decode([{...app, message: 'injected', kind: 'selection'}]);
  assert.equal(c.kind, 'catalog');
  if (c.kind === 'catalog') assert.deepEqual(currentExample(c, 'Target'), app);
});
