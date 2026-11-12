import test from 'node:test';
import assert from 'node:assert/strict';
import {renderPreviewHtml} from '../src/previewHtml.ts';
import type {PreviewInput} from '../src/previewHtml.ts';

const input: PreviewInput = {kind: 'module', skin: 'web', id: 'document', nonce: 'N', cspSource: "'self'",
  jsUri: 'https://resource.invalid/Demo.js', wasmUri: 'https://resource.invalid/Demo.wasm', exportName: 'Demo'};
test('document pins CSP, nonce, cache identity, language absence, CSS order, script order', () => {
  const html = renderPreviewHtml(input);
  assert.match(html, /default-src &#39;none&#39;; script-src &#39;nonce-N&#39; &#39;wasm-unsafe-eval&#39;; connect-src &#39;self&#39;; style-src &#39;nonce-N&#39;/);
  assert.equal((html.match(/nonce="N"/g) || []).length, 4);
  assert.match(html, /Demo.js\?v=document/); assert.match(html, /Demo.wasm\?v=document/);
  assert.doesNotMatch(html, /\blang=/);
  assert.match(html, /background:Canvas/);
  assert.ok(html.indexOf('body.vscode-high-contrast-light') > html.indexOf('body.vscode-dark'));
  assert.ok(html.indexOf("addEventListener('message'") < html.indexOf('src="https:'));
  assert.ok(html.indexOf('src="https:') < html.indexOf('const factory'));
  assert.equal((html.match(/completed.then/g) || []).length, 1);
});
test('HTML text, attribute, inline script escaping remain distinct', () => {
  const hostile = '</script><img src=x onerror="bad">&\u2028\u2029';
  const message = renderPreviewHtml({kind: 'message', id: 'id', nonce: 'n"', cspSource: "'self'", text: hostile});
  assert.match(message, /default-src &#39;none&#39;; style-src &#39;nonce-n&quot;&#39;/);
  assert.doesNotMatch(message, /script-src|connect-src/);
  assert.doesNotMatch(message, /<script/); assert.match(message, /&lt;\/script&gt;/);
  assert.match(message, /nonce="n&quot;"/);
  const html = renderPreviewHtml({...input, id: hostile, exportName: hostile, jsUri: 'a"&<.js'});
  assert.match(html, /src="a&quot;&amp;&lt;.js/);
  assert.doesNotMatch(html, /<img/);
  assert.match(html, /\\u003c\/script>/); assert.match(html, /\\u2028\\u2029/);
});
