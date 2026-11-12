import type * as vscode from 'vscode';
import {existsSync, readFileSync} from 'node:fs';
import {dirname, resolve} from 'node:path';
import {randomUUID} from 'node:crypto';
import {createPreview} from './preview.ts';
import type {PreviewDoors, PreviewPanel} from './preview.ts';
import {readCatalog, currentExample} from './catalog.ts';
import {currentSkin} from './skin.ts';
import {skinNames} from './skins.ts';
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
 * Cost: activation once; fixed ten subscriptions, no task or workspace scans. */
export function wire(api: typeof vscode, context: vscode.ExtensionContext): vscode.Disposable {
  let current: Slot | undefined;
  const output = api.window.createOutputChannel('Loka Preview');
  const status = api.window.createStatusBarItem(api.StatusBarAlignment.Left);
  status.show();
  /** Last-resort asynchronous diagnostic; even a broken diagnostic service cannot reject.
   * Cost: once per asynchronous fault or cleanup failure; one contained output attempt, no rows. */
  const fault = (error: unknown): void => {
    try { output.appendLine('Loka Preview fault: ' + String(error)); }
    catch { /* The fault sink must remain nonthrowing even when reporting is unavailable. */ }
  };
  /** Cost: Build START / Preview replacement once; read one current trust fact. */
  const admitted = (): boolean => api.workspace.isTrusted;
  /** Cost: each settings/task/preview preparation; select the first folder without scanning. */
  const folder = () => api.workspace.workspaceFolders?.[0];
  /** Cost: each task or replacement once; read one folder-scoped setting. */
  const setting = (name: 'buildDirectory' | 'configureCommand'): string =>
    api.workspace.getConfiguration('loka.preview', folder()?.uri).get<string>(name)!;
  /** Cost: each owner status publication; one status item write. */
  const report = (message: string): void => { status.text = 'Loka: ' + message; };
  /** Cost: each selection read/write; one folder-qualified preference key. */
  const selectionKey = (workspace: vscode.WorkspaceFolder): string => 'loka.preview.example:' + workspace.uri.toString();
  /** Cost: each skin read/write; one folder-qualified preference key. */
  const skinKey = (workspace: vscode.WorkspaceFolder): string => 'loka.preview.skin:' + workspace.uri.toString();
  /** Cost: each admitted Select/Build/replacement; one file, linear decoder, no retained descriptor. */
  const catalog = () => {
    const directory = resolve(folder()!.uri.fsPath, setting('buildDirectory'));
    return {directory, value: readCatalog(resolve(directory, 'loka-web-apps.json'), file => readFileSync(file, 'utf8'))};
  };
  /** Cost: each admitted Build/replacement; two preference reads, a skin-name scan and catalog scan. */
  const prepare = () => {
    const {directory, value} = catalog();
    if (value.kind !== 'catalog') return value;
    const app = currentExample(value, context.workspaceState.get<string>(selectionKey(folder()!)));
    return 'message' in app ? app : {app, directory, skin: currentSkin(context.workspaceState.get(skinKey(folder()!)))};
  };
  /** Cost: command/task/Select settlement; borrow the current slot once, never capture a panel. */
  const reload = (): void => { current?.doors.reload(); };
  const build = createBuild<vscode.TaskExecution>({admitted, fault, report, reload,
    /** Cost: admitted idle start once; Configure reads one setting, Build prepares one row. */
    prepare(kind) {
      const workspace = folder();
      if (!workspace) return {message: 'Open a workspace folder'};
      const cwd = workspace.uri.fsPath;
      if (kind === 'CONFIGURE') return {kind, cwd, command: setting('configureCommand')};
      const prepared = prepare();
      return 'message' in prepared ? prepared : {kind, cwd, directory: prepared.directory, target: prepared.app.target};
    },
    /** Cost: admitted task once; construct/execute one task from the completed operation. */
    execute(operation) {
      return api.tasks.executeTask(new api.Task({type: 'loka'}, folder()!,
        operation.kind === 'CONFIGURE' ? 'Configure' : 'Build', 'Loka',
        operation.kind === 'CONFIGURE'
          ? new api.ShellExecution(operation.command, {cwd: operation.cwd})
          : new api.ProcessExecution('cmake', ['--build', operation.directory, '--target', operation.target], {cwd: operation.cwd})));
    },
  });
  /** Cost: Select once; one picker, at most one preference write and current-slot reload.
   * Like selectSkin below, the captured key belongs to the opening folder. Late success only borrows the slot,
   * which shutdown clears before releasing UI. Late failures use only the window service. */
  const select = async (): Promise<void> => {
    try {
      if (!admitted()) { await api.window.showErrorMessage('Trust the workspace to select an example'); return; }
      const workspace = folder();
      if (!workspace) { await api.window.showErrorMessage('Open a workspace folder'); return; }
      const key = selectionKey(workspace);
      const {value} = catalog();
      if (value.kind !== 'catalog') { await api.window.showErrorMessage(value.message); return; }
      const picked = await api.window.showQuickPick(value.apps.map(app => ({label: app.name, description: app.target, id: app.id})));
      if (!picked) return;
      await context.workspaceState.update(key, picked.id);
      reload();
    } catch (error) {
      try { await api.window.showErrorMessage('Select Example failed: ' + String(error)); }
      catch { /* Reporting failure must not escape the command's asynchronous continuation. */ }
    }
  };
  /** Cost: Select Skin once; one picker over generated names, at most one save and reload.
   * Like select above, capture the folder key before awaiting; borrow only the current
   * slot after saving. Shutdown clears that slot before releasing UI. */
  const selectSkin = async (): Promise<void> => {
    try {
      const workspace = folder();
      if (!workspace) { await api.window.showErrorMessage('Open a workspace folder'); return; }
      const key = skinKey(workspace);
      const requested = currentSkin(context.workspaceState.get(key));
      const picked = await api.window.showQuickPick(skinNames.map(name => ({
        label: name, description: name === requested ? 'Requested' : '',
      })));
      if (!picked) return;
      await context.workspaceState.update(key, picked.label);
      reload();
    } catch (error) {
      try { await api.window.showErrorMessage('Select Skin failed: ' + String(error)); }
      catch { /* Reporting failure must not escape the command's asynchronous continuation. */ }
    }
  };
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
        admitted, folder: () => folder()?.uri.fsPath, prepare, resolve, directory: dirname, existsSync,
        asWebviewUri: file => adapter.asWebviewUri(file), cspSource: adapter.cspSource,
        identity: randomUUID, clock: {after: (ms, callback) => setTimeout(callback, ms), cancel: clearTimeout},
        output: message => output.appendLine(message), status: report,
        error: message => { void Promise.resolve(api.window.showErrorMessage(message)).catch(fault); }, fault,
      }, doors => adapter.connect(doors));
    }),
    api.commands.registerCommand('loka.preview.build', () => build.start()),
    api.commands.registerCommand('loka.preview.reload', reload),
    api.commands.registerCommand('loka.preview.configure', () => build.start('CONFIGURE')),
    api.commands.registerCommand('loka.preview.selectExample', select),
    api.commands.registerCommand('loka.preview.selectSkin', selectSkin),
  ];
  /** Cost: activation shutdown once, not fault retirement; retire current, close its healthy owner
   * to release its timer, then attempt each of ten activation releases. */
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
