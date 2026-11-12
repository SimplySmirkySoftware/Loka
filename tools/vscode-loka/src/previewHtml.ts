/** One document's immutable rendering input; identity and nonce are independent. */
export type PreviewInput = Readonly<{
  id: string; nonce: string; cspSource: string;
} & ({kind: 'module'; skin: string; jsUri: string; wasmUri: string; exportName: string}
  | {kind: 'message'; text: string})>;

function text(value: string): string {
  return value.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}
function attribute(value: string): string {
  return text(value).replaceAll('"', '&quot;').replaceAll("'", '&#39;');
}
function script(value: string): string {
  return JSON.stringify(value).replaceAll('<', '\\u003c')
    .replaceAll('\u2028', '\\u2028').replaceAll('\u2029', '\\u2029');
}

/** Render one complete document without I/O.
 * Cost: Preview replacement once per assignment; no owner rows walked. */
export function renderPreviewHtml(input: PreviewInput): string {
  const nonce = attribute(input.nonce);
  const csp = attribute(input.kind === 'message'
    ? "default-src 'none'; style-src 'nonce-" + input.nonce + "'"
    : "default-src 'none'; script-src 'nonce-" + input.nonce
    + "' 'wasm-unsafe-eval'; connect-src " + input.cspSource
    + "; style-src 'nonce-" + input.nonce + "'");
  const head = '<!doctype html><html><head><meta charset="utf-8">'
    + '<meta http-equiv="Content-Security-Policy" content="' + csp + '">'
    + '<style nonce="' + nonce + '">html,body{margin:0;padding:0}body{background:Canvas}\n'
    + 'body.vscode-light{color-scheme:light}\n'
    + 'body.vscode-dark,body.vscode-high-contrast{color-scheme:dark}\n'
    + 'body.vscode-high-contrast-light{color-scheme:light}</style></head><body>';
  if (input.kind === 'message') return head + text(input.text) + '</body></html>';
  const key = '?v=' + encodeURIComponent(input.id);
  return head + '<script nonce="' + nonce + '">' + `
const host = acquireVsCodeApi();
const id = ${script(input.id)};
let slot = {kind: 'PENDING'};
let disposalRequested = false;
/** Cost: document events, one host message per fact; no resource rows walked. */
const post = (type, fields = {}) => host.postMessage({id, type, ...fields});
const report = error => post('error', {reason: String(error)});
addEventListener('error', event => report(event.message || 'Script load failed'));
addEventListener('unhandledrejection', event => report(event.reason));
const acquisitionFailed = error => {
  if (slot.kind !== 'PENDING') { report(error); return; }
  slot = {kind: 'NONE'};
  report(error);
  if (disposalRequested) post('absent');
};
/** Cost: host disposal at most once per document; one module disposal, no row walk. */
addEventListener('message', event => {
  if (!event.data || event.data.id !== id || event.data.type !== 'dispose' || disposalRequested) return;
  disposalRequested = true;
  if (slot.kind === 'READY') slot.module.web.dispose();
  else if (slot.kind === 'NONE') post('absent');
});
` + '</script><script nonce="' + nonce + '" src="' + attribute(input.jsUri + key)
    + '"></script><script nonce="' + nonce + '">' + `
try {
  const factory = window[${script(input.exportName)}];
  if (typeof factory !== 'function') throw new Error('Module factory is unavailable');
  Promise.resolve(factory({locateFile: () => ${script(input.wasmUri + key)}, lokaSkin: ${script(input.skin)}})).then(module => {
    slot = {kind: 'READY', module};
    module.web.completed.then(() => post('completed'));
    try {
      if (disposalRequested) module.web.dispose();
      else {
        const status = module.web.start();
        post('started', {status});
        if (status === 2) report('Module start failed');
      }
    } catch (error) { report(error); }
  }, acquisitionFailed);
} catch (error) { acquisitionFailed(error); }
` + '</script></body></html>';
}
