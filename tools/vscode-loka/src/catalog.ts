/** Completed catalog facts; array order is the producer's default preference. */
export type App = Readonly<{id: string; name: string; target: string; exportName: string; sourceDir: string; js: string}>;
export type Diagnostic = Readonly<{kind: 'missing' | 'malformed' | 'unsupported' | 'empty' | 'unreadable' | 'selection'; message: string}>;
export type Catalog = Readonly<{kind: 'catalog'; apps: readonly App[]}>;

/** Cost: preparation once; scan each path's components, with the producer's lexical bases. */
function relativePath(value: string): boolean {
  return !value.startsWith('/') && !value.startsWith('~')
    && !/^[A-Za-z]:/.test(value) && !value.includes('\\') && !value.includes('$<')
    && !value.split('/').includes('..');
}
/** Cost: one input; one diagnostic value, no retained state. */
function invalid(kind: Diagnostic['kind']): Diagnostic {
  return {kind, message: kind === 'selection' ? 'Selected example is absent; use Select Example'
    : 'Web catalog ' + kind + '; run Configure'};
}
/** Pure synchronous decoder. Cost: one JSON file per admitted preparation, linear in bytes and rows. */
export function decodeCatalog(text: string): Catalog | Diagnostic {
  let value: unknown;
  try { value = JSON.parse(text); } catch { return invalid('malformed'); }
  if (!value || typeof value !== 'object' || Array.isArray(value)) return invalid('malformed');
  const doc = value as Record<string, unknown>;
  if (typeof doc.version !== 'number') return invalid('malformed');
  if (doc.version !== 1) return invalid('unsupported');
  if (!Array.isArray(doc.apps)) return invalid('malformed');
  const apps: App[] = [], seen = new Set<string>();
  for (const raw of doc.apps) {
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) return invalid('malformed');
    const row = raw as Record<string, unknown>;
    if (!['id', 'name', 'target', 'exportName', 'sourceDir', 'js'].every(key => typeof row[key] === 'string')) return invalid('malformed');
    const app = row as App;
    if (app.target.length === 0 || /[^A-Za-z0-9_]/.test(app.target)) return invalid('malformed');
    if (!/^[A-Za-z_]/.test(app.exportName) || /[^A-Za-z0-9_]/.test(app.exportName)) return invalid('malformed');
    if (app.id !== app.target) return invalid('malformed');
    if (seen.has(app.id)) return invalid('malformed');
    if (!relativePath(app.sourceDir) || !app.sourceDir.startsWith('example/') || app.sourceDir.length <= 8) return invalid('malformed');
    if (!relativePath(app.js) || !app.js.endsWith('.js')) return invalid('malformed');
    seen.add(app.id);
    apps.push(Object.freeze({id: app.id, name: app.name, target: app.target,
      exportName: app.exportName, sourceDir: app.sourceDir, js: app.js}));
  }
  return apps.length ? {kind: 'catalog', apps: Object.freeze(apps)} : invalid('empty');
}
/** Expected filesystem failures never enter an owner's mailbox. Caller admits trust first.
 * Cost: preparation once; one synchronous read then one decode, no cache. */
export function readCatalog(file: string, read: (file: string) => string): Catalog | Diagnostic {
  let text: string;
  try { text = read(file); } catch (error) {
    return invalid(error && typeof error === 'object' && 'code' in error && error.code === 'ENOENT' ? 'missing' : 'unreadable');
  }
  return decodeCatalog(text);
}
/** Cost: Build/replacement once; scan this completed catalog only, preserving its order. */
export function currentExample(catalog: Catalog, identity: string | undefined): App | Diagnostic {
  if (!catalog.apps.length) return invalid('empty');
  if (!identity) return catalog.apps[0];
  return catalog.apps.find(app => app.id === identity) ?? invalid('selection');
}
