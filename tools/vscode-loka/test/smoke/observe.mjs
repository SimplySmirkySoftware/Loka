import flow from './flow.cjs';

// Pin-dependent workbench rows. Hidden widgets remain in the DOM after acceptance.
export const pickerExpression = `(() => {
  const widgets = [...document.querySelectorAll('.quick-input-widget')].filter(w =>
    getComputedStyle(w).display !== 'none' && getComputedStyle(w).visibility !== 'hidden' && w.getBoundingClientRect().width > 0);
  const w = widgets[0], rows = w ? [...w.querySelectorAll('.quick-input-list .monaco-list-row')] : [];
  return {visible: widgets.length, input: !!w && w.contains(document.activeElement) && document.activeElement.tagName === 'INPUT',
    labels: rows.map(r => r.getAttribute('aria-label')), focused: rows.flatMap((r, i) => r.classList.contains('focused') ? [i] : [])};
})()`;

export function pickerMatches(value, expected) {
  if (!value) return false;
  if (expected.closed) return value.visible === 0;
  return value.visible === 1 && value.input === true
    && JSON.stringify(value.labels) === JSON.stringify(expected.labels)
    && JSON.stringify(value.focused) === JSON.stringify([expected.focus]);
}

// Evaluate in the outer iframe, never the workbench. Reacquire active-frame and body every time.
export const webviewExpression = `(() => {
  try {
    const d = document.querySelector('iframe#active-frame')?.contentDocument;
    if (!d?.body) return null;
    const b = d.body.getBoundingClientRect();
    const all = [...d.body.querySelectorAll('*')];
    const rows = all.slice(0, 2048).map(e => {
      const r = e.getBoundingClientRect(), style = d.defaultView.getComputedStyle(e);
      return {tag: e.tagName.toLowerCase(), text: (e.innerText ?? '').trim().slice(0, 256), leaf: e.children.length === 0,
        x: Math.round(r.left - b.left), y: Math.round(r.top - b.top), width: r.width, height: r.height,
        visible: style.display !== 'none' && style.visibility !== 'hidden' && style.visibility !== 'collapse' && e.checkVisibility()};
    });
    return {text: d.body.innerText.trim().slice(0, 4096), scripts: d.querySelectorAll('script').length, total: all.length, rows};
  } catch { return null; }
})()`;

/** Cost: per match or failure diagnostic, scan at most 2048 snapshot rows per fixture in the launcher. */
function markerRows(value) {
  return Object.fromEntries(Object.entries(flow.markers).map(([id, fixture]) => [id,
    (value?.rows ?? []).filter(row => (fixture.selector === '*' || row.tag === fixture.selector)
      && (!fixture.leaf || row.leaf) && row.text === fixture.text)]));
}

export function webviewMatches(value, status, expected) {
  if (!value || !Array.isArray(value.rows) || status !== expected.status) return false;
  // A capped snapshot cannot prove "exactly one marker" or "no marker": refuse it rather than judge a part.
  if (value.total !== value.rows.length) return false;
  const markers = markerRows(value);
  if (expected.kind === 'message') return value.text === expected.text && value.scripts === 0
    && Object.values(markers).every(rows => rows.length === 0);
  const rows = markers[expected.id];
  return rows?.length === 1 && rows[0].visible === true && rows[0].width > 0 && rows[0].height > 0
    && Math.abs(rows[0].x - expected.offset) <= 1 && Math.abs(rows[0].y - expected.offset) <= 1;
}

export function webviewDiagnostic(value) {
  return {status: value?.status ?? null,
    markers: Object.fromEntries(Object.entries(markerRows(value?.body)).map(([id, rows]) =>
      [id, rows.map(({x, y, visible}) => ({id, x, y, visible}))])),
    scripts: value?.body?.scripts ?? null, text: value?.body?.text?.slice(0, 60) ?? null};
}

/** Cost: each poll lists targets once, opens at most one local connection and scans one body.
 * Registration precedes open so even a failed or interrupted connection is closed. */
export async function readWebview(owner, targets, connection) {
  owner.check();
  const candidates = (await targets()).filter(t => t.type === 'iframe' && t.url.startsWith('vscode-webview://'));
  if (candidates.length > 1) throw new Error('Expected exactly one outer webview target; last read ' + candidates.length);
  if (!candidates.length) return null;
  let local;
  const close = () => { const outgoing = local; local = undefined; outgoing?.close(); };
  owner.abort?.signal.addEventListener('abort', close, {once: true});
  try {
    local = connection(candidates[0]);
    await local.open();
    owner.check();
    return await local.evaluate(webviewExpression);
  } catch (error) {
    owner.check(); // Cancellation must not become another waiting observation.
    return {unreadable: error.message};
  } finally { owner.abort?.signal.removeEventListener('abort', close); close(); }
}

/** Cost: one step; bounded polls, diagnostics retain only the last immutable read. */
export async function observe(owner, read, matches, expected, deadline, label, diagnostic = value => value) {
  let last;
  try {
    await owner.wait(async () => { last = await read(); return matches(last); }, deadline, label);
  } catch (error) {
    throw new Error(`${error.message}; expected ${JSON.stringify(expected)}, last read ${JSON.stringify(diagnostic(last))}`);
  }
}
