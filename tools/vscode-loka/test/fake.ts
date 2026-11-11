import assert from 'node:assert/strict';
import type * as vscode from 'vscode';
import {mkdtempSync, mkdirSync, writeFileSync, rmSync} from 'node:fs';
import {pathToFileURL} from 'node:url';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

/** Refuse unmodelled API reads and writes, including accidental native calls. */
export function strict<T extends object>(value: T): T {
  return new Proxy(value, {
    get(target, key, receiver) {
      assert.ok(Reflect.has(target, key), 'unknown API read: ' + String(key));
      return Reflect.get(target, key, receiver);
    },
    set(target, key, next, receiver) {
      assert.ok(Reflect.has(target, key), 'unknown API write: ' + String(key));
      return Reflect.set(target, key, next, receiver);
    },
  });
}
export function fakeHost() {
  const root = mkdtempSync(join(tmpdir(), 'loka-wire-'));
  mkdirSync(join(root, 'module'));
  for (const ext of ['js', 'wasm']) writeFileSync(join(root, 'module', 'Demo.' + ext), 'fixture');
  const subscriptions: {label: string; attempts: number; active: boolean}[] = [];
  const hooks = {
    options: (_panel: FakePanel) => {}, html: (_panel: FakePanel) => {},
    disposeRegistration: (_panel: FakePanel) => {}, messageRegistration: (_panel: FakePanel) => {},
    release: (_label: string) => {}, nativeDispose: (_panel: FakePanel) => {},
    status: (_text: string) => {}, output: (_text: string) => {},
    postMessage: (): PromiseLike<boolean> => Promise.resolve(true),
    execute: () => {}, error: () => Promise.resolve(undefined),
  };
  const subscription = (label: string) => {
    const row = {label, attempts: 0, active: true}; subscriptions.push(row);
    return strict({dispose() { ++row.attempts; row.active = false; hooks.release(label); }});
  };
  const commands = new Map<string, () => void>();
  const statuses: string[] = [], output: string[] = [];
  const panels: FakePanel[] = [], tasks: unknown[] = [];
  const processListeners: ((e: {execution: object; exitCode?: number}) => void)[] = [];
  const taskListeners: ((e: {execution: object}) => void)[] = [];
  let trusted = true;
  let resolveTask!: (execution: object) => void;
  let rejectTask!: (error: unknown) => void;
  class Disposable {
    private action: (() => void) | undefined;
    constructor(action: () => void) { this.action = action; }
    dispose() { const action = this.action; action?.(); this.action = undefined; }
  }
  const uri = (file: string) => strict({fsPath: file, toString: () => pathToFileURL(file).href});
  const folder = strict({uri: uri(root), name: 'fixture', index: 0});
  const api = strict({
    __esModule: false, // CommonJS __importStar probes this marker before copying exports.
    Disposable, Uri: strict({file: uri}), ViewColumn: strict({Beside: 2}), StatusBarAlignment: strict({Left: 1}),
    workspace: strict({get isTrusted() { return trusted; }, workspaceFolders: [folder],
      getConfiguration(section: string, scope: unknown) {
        assert.equal(section, 'loka.preview'); assert.equal(scope, folder.uri);
        return strict({get(name: string) {
          if (name === 'module') return 'module/Demo.js';
          assert.equal(name, 'buildCommand'); return 'build fixture && finish';
        }});
      },
    }),
    window: strict({
      createOutputChannel(name: string) { assert.equal(name, 'Loka Preview'); return strict({
        ...subscription('output'), appendLine(text: string) { hooks.output(text); output.push(text); },
      }); },
      createStatusBarItem(alignment: number) { assert.equal(alignment, 1); return strict({
        ...subscription('status'), show() {}, set text(text: string) { hooks.status(text); statuses.push(text); },
      }); },
      showErrorMessage(_message: string) { return hooks.error(); },
      createWebviewPanel(type: string, title: string, column: number, options: unknown) {
        assert.equal(type, 'loka.preview'); assert.equal(title, 'Loka Preview'); assert.equal(column, 2);
        const p = new FakePanel(options, subscription, hooks); panels.push(p); return p.native;
      },
    }),
    commands: strict({registerCommand(name: string, callback: () => void) {
      assert.equal(commands.has(name), false); commands.set(name, callback); return subscription(name);
    }}),
    ShellExecution: class { command: string; options: unknown;
      constructor(command: string, options: unknown) { this.command = command; this.options = options; } },
    Task: class { definition: unknown; scope: unknown; name: string; source: string; execution: unknown;
      constructor(definition: unknown, scope: unknown, name: string, source: string, execution: unknown) {
        this.definition = definition; this.scope = scope; this.name = name; this.source = source; this.execution = execution;
      } },
    tasks: strict({
      executeTask(task: unknown) { tasks.push(task); hooks.execute();
        return new Promise<object>((yes, no) => { resolveTask = yes; rejectTask = no; }); },
      onDidEndTaskProcess(callback: (e: {execution: object; exitCode?: number}) => void) {
        processListeners.push(callback); return subscription('process');
      },
      onDidEndTask(callback: (e: {execution: object}) => void) { taskListeners.push(callback); return subscription('task'); },
    }),
  });
  const context = strict({subscriptions: [] as vscode.Disposable[]}) as unknown as vscode.ExtensionContext;
  return {api: api as unknown as typeof vscode, context, root, folder, hooks, subscriptions, panels, tasks, commands,
    statuses, output, trust(value: boolean) { trusted = value; },
    command(name: string) { const command = commands.get('loka.preview.' + name); assert.ok(command); command(); },
    resolve(execution: object) { resolveTask(execution); }, reject(error: unknown) { rejectTask(error); },
    ended(execution: object, exitCode = 0) { processListeners.forEach(f => f({execution, exitCode})); },
    taskEnded(execution: object) { taskListeners.forEach(f => f({execution})); },
    cleanup() { rmSync(root, {recursive: true, force: true}); },
  };
}
type Hooks = ReturnType<typeof fakeHost>['hooks'];
export class FakePanel {
  native: vscode.WebviewPanel;
  creation: unknown;
  options: unknown;
  html = '';
  optionsWrites = 0;
  htmlWrites = 0;
  disposeAttempts = 0;
  reveals = 0;
  disposed = false;
  posts: unknown[] = [];
  conversions: string[] = [];
  closes: (() => void)[] = [];
  messages: ((raw: unknown) => void)[] = [];
  constructor(creation: unknown, subscription: (label: string) => {dispose(): void}, hooks: Hooks) {
    this.creation = creation;
    const use = () => assert.equal(this.disposed, false, 'native use after disposal');
    const self = this;
    this.native = strict({
      dispose() { ++self.disposeAttempts; hooks.nativeDispose(self); self.destroy(); },
      reveal(column: number) { use(); assert.equal(column, 2); ++self.reveals; },
      onDidDispose(callback: () => void) {
        use(); self.closes.push(callback); hooks.disposeRegistration(self); return subscription('panel-close');
      },
      webview: strict({
        get cspSource() { use(); return "'self' https://*.vscode-cdn.net"; },
        asWebviewUri(file: {fsPath: string; toString(): string}) {
          use(); self.conversions.push(file.toString());
          return {toString: () => 'https://resource.invalid' + file.fsPath};
        },
        set options(value: unknown) { use(); ++self.optionsWrites; self.options = value; hooks.options(self); },
        set html(value: string) { use(); ++self.htmlWrites; self.html = value; hooks.html(self); },
        postMessage(raw: unknown) { use(); self.posts.push(raw); return hooks.postMessage(); },
        onDidReceiveMessage(callback: (raw: unknown) => void) {
          use(); self.messages.push(callback); hooks.messageRegistration(self); return subscription('panel-message');
        },
      }),
    }) as unknown as vscode.WebviewPanel;
  }
  destroy() { this.disposed = true; this.closes.forEach(f => f()); }
  receive(type: string, fields = {}) {
    const id = /const id = "([^"]+)"/.exec(this.html)?.[1];
    this.messages.forEach(f => f({id, type, ...fields}));
  }
}
