import type * as vscode from 'vscode';
import {existsSync} from 'node:fs';
import {basename, dirname, resolve} from 'node:path';
import {randomUUID} from 'node:crypto';
import {createPreview} from './preview.ts';
import type {PreviewDoors, PreviewPanel} from './preview.ts';
import {createBuild} from './build.ts';

type PanelAdapter = PreviewPanel & Readonly<{
  connect(doors: PreviewDoors): void;
  reveal(): void;
  asWebviewUri(file: string): string;
  cspSource: string;
}>;
type Slot = Readonly<{doors: PreviewDoors; adapter: PanelAdapter}>;

/** Cost: one adapter or activation subscription; one contained release attempt. */
function release(subscription: vscode.Disposable, fault: (error: unknown) => void): void {
  try { subscription.dispose(); } catch (error) { fault(error); }
}

/** Internal native resource owner, also used directly by the adapter contract tests.
 * Cost: one per panel; owns only its capability and two native subscriptions. */
export function createPanelAdapter(api: typeof vscode, panel: vscode.WebviewPanel,
  slot: {get(): Slot | undefined; set(value: Slot | undefined): void},
  fault: (error: unknown) => void): PanelAdapter {
  let live: {panel: vscode.WebviewPanel; subscriptions: vscode.Disposable[]} | undefined = {
    panel, subscriptions: [],
  };
  /** Cost: native close or fault once; detach before releasing this adapter's two rows. */
  const take = (): boolean => {
    if (!live) return false;
    const outgoing = live;
    live = undefined;
    if (slot.get()?.adapter === adapter) slot.set(undefined);
    const subscriptions = outgoing.subscriptions.splice(0);
    for (const subscription of subscriptions) release(subscription, fault);
    return true;
  };
  /** Cost: connect twice at most; append one own subscription or release a late return. */
  const keep = (subscription: vscode.Disposable): void => {
    if (live) live.subscriptions.push(subscription); else release(subscription, fault);
  };
  const adapter: PanelAdapter = {
    /** Cost: INITIAL_INSTALL once; publish one slot and register at most two listeners. */
    connect(doors) {
      slot.set({doors, adapter});
      if (!live) return;
      keep(live.panel.onDidDispose(() => { if (take()) doors.close(); }));
      if (!live) return;
      keep(live.panel.webview.onDidReceiveMessage(raw => doors.receive(raw)));
    },
    /** Cost: Preview replacement once; map its zero/one root and perform at most two writes. */
    install(prepared) {
      if (!live) return 'closed';
      live.panel.webview.options = {enableScripts: prepared.options.enableScripts,
        localResourceRoots: prepared.options.localResourceRoots.map(file => api.Uri.file(file))};
      if (!live) return 'closed';
      live.panel.webview.html = prepared.html;
      return live ? 'installed' : 'closed';
    },
    /** Cost: Preview reload once; one post and one rejection handler, no rows. */
    postDispose(id) {
      if (!live) return 'closed';
      void Promise.resolve(live.panel.webview.postMessage({id, type: 'dispose'})).catch(fault);
      return 'posted';
    },
    /** Cost: mailbox fault or activation shutdown once; take own subscriptions and attempt one native disposal. */
    retire() {
      const native = live?.panel;
      if (take()) {
        try { native!.dispose(); } catch (error) { fault(error); }
      }
    },
    /** Cost: Open with an occupied slot; one native reveal, no rows. */
    reveal() { live?.panel.reveal(api.ViewColumn.Beside); },
    /** Cost: replacement twice; one native URI conversion, no rows. */
    asWebviewUri(file) { return live ? live.panel.webview.asWebviewUri(api.Uri.file(file)).toString() : ''; },
    /** Cost: Preview construction once; one native source read, no rows. */
    get cspSource() { return live ? live.panel.webview.cspSource : ''; },
  };
  return adapter;
}

/** Activation owns Build, the optional Preview and its UI subscriptions.
 * Cost: activation once; fixed seven subscriptions, no task or workspace scans. */
export function wire(api: typeof vscode, _context: vscode.ExtensionContext): vscode.Disposable {
  let current: Slot | undefined;
  const output = api.window.createOutputChannel('Loka Preview');
  const status = api.window.createStatusBarItem(api.StatusBarAlignment.Left);
  status.show();
  /** Last-resort asynchronous diagnostic; even a broken diagnostic service cannot reject.
   * Cost: once per asynchronous fault or cleanup failure; one output line, no rows. */
  const fault = (error: unknown): void => {
    try { output.appendLine('Loka Preview fault: ' + String(error)); }
    catch { /* The fault sink must remain nonthrowing even when reporting is unavailable. */ }
  };
  /** Cost: Build START / Preview replacement once; read one current trust fact. */
  const admitted = (): boolean => api.workspace.isTrusted;
  /** Cost: each settings/task/preview preparation; select the first folder without scanning. */
  const folder = () => api.workspace.workspaceFolders?.[0];
  /** Cost: each task or replacement once; read one folder-scoped setting. */
  const setting = (name: 'module' | 'buildCommand'): string =>
    api.workspace.getConfiguration('loka.preview', folder()?.uri).get<string>(name)!;
  /** Cost: each owner status publication; one status item write. */
  const report = (message: string): void => { status.text = 'Loka: ' + message; };
  const build = createBuild<vscode.TaskExecution>({admitted, fault, report,
    /** Cost: admitted Build START once; construct/execute one task, no task rows walked. */
    execute() {
      const workspace = folder();
      if (!workspace) throw new Error('Open a workspace folder');
      return api.tasks.executeTask(new api.Task({type: 'loka'}, workspace, 'Build', 'Loka',
        new api.ShellExecution(setting('buildCommand'), {cwd: workspace.uri.fsPath})));
    },
    /** Cost: successful owned task once; read the activation slot at delivery time. */
    reload() { current?.doors.reload(); },
  });
  // Each native event forwards one owner event; each command reads the current slot once.
  // Preview fs ports perform two synchronous existence checks per replacement; path/UUID ports are constant work.
  // Clock ports schedule/cancel at most one Preview timer; message/output sinks write once per publication.
  const subscriptions = [output, status,
    api.tasks.onDidEndTaskProcess(event => build.ended(event.execution, event.exitCode)),
    api.tasks.onDidEndTask(event => build.taskEnded(event.execution)),
    api.commands.registerCommand('loka.preview.open', () => {
      if (current) { current.adapter.reveal(); return; }
      const adapter = createPanelAdapter(api,
        api.window.createWebviewPanel('loka.preview', 'Loka Preview', api.ViewColumn.Beside,
          {enableScripts: false, retainContextWhenHidden: true, localResourceRoots: []}),
        {get: () => current, set: value => { current = value; }}, fault);
      createPreview(adapter, {
        admitted, folder: () => folder()?.uri.fsPath, setting,
        resolve, directory: dirname, stem: file => basename(file, '.js'), existsSync,
        asWebviewUri: file => adapter.asWebviewUri(file), cspSource: adapter.cspSource,
        identity: randomUUID, clock: {after: (ms, callback) => setTimeout(callback, ms), cancel: clearTimeout},
        output: message => output.appendLine(message), status: report,
        error: message => { void Promise.resolve(api.window.showErrorMessage(message)).catch(fault); }, fault,
      }, doors => adapter.connect(doors));
    }),
    api.commands.registerCommand('loka.preview.build', () => build.start()),
    api.commands.registerCommand('loka.preview.reload', () => current?.doors.reload()),
  ];
  /** Cost: activation shutdown once, not fault retirement; retire current, close its healthy owner
   * to release its timer, then attempt each of seven activation releases. */
  return new api.Disposable(() => {
    build.dispose();
    if (current) {
      const outgoing = current;
      outgoing.adapter.retire();
      try { outgoing.doors.close(); } catch (error) { fault(error); }
    }
    for (const subscription of subscriptions) release(subscription, fault);
  });
}
