import {createMailbox} from './mailbox.ts';
import {renderPreviewHtml} from './previewHtml.ts';
import type {PreviewInput} from './previewHtml.ts';

/** One prepared document supplies both native assignments. */
export type PreparedPreview = Readonly<{
  options: {enableScripts: boolean; localResourceRoots: readonly string[]};
  html: string;
}>;
/** Adapter-owned capability, revoked synchronously before native disposal posts CLOSE. */
export type PreviewPanel = Readonly<{
  /** Cost: one replacement; at most two native writes, checking capability between them and afterwards. */
  install(prepared: PreparedPreview): 'installed' | 'closed';
  /** Cost: one reload; at most one native post while the capability is live, not an acknowledgement. */
  postDispose(id: string): 'posted' | 'closed';
  /** Idempotent fault cleanup. Cost: one capability reference, no owner events. */
  revoke(): void;
}>;
/** Host services borrowed for the preview lifetime; clock callbacks are asynchronous.
 * Cost: admission once per replacement; one setting and file existence twice. */
export type PreviewPorts<T> = Readonly<{
  admitted(): boolean;
  folder(): string | undefined;
  setting(name: 'module'): string;
  resolve(folder: string, relative: string): string;
  directory(file: string): string;
  stem(file: string): string;
  existsSync(file: string): boolean;
  asWebviewUri(file: string): string;
  cspSource: string;
  identity(): string;
  clock: {after(ms: number, callback: () => void): T; cancel(timer: T): void};
  output(message: string): void;
  status(message: string): void;
  error(message: string): void;
  /** Nonthrowing asynchronous fault sink. Cost: once per failed drain. */
  fault(error: unknown): void;
}>;
type Phase<T> = Readonly<
  {kind: 'MESSAGE'; text: string} | {kind: 'LOADING'}
  | {kind: 'RUNNING'; status: 0 | 1} | {kind: 'FAILED'; reason: string}
  | {kind: 'ENDED'} | {kind: 'DISPOSING'; timer: T} | {kind: 'CLOSED'}>;
type State<T> = Readonly<{id: string; phase: Phase<T>}>;

type Event<T> = Readonly<{kind: 'INITIAL_INSTALL' | 'RELOAD' | 'CLOSE'}
  | {kind: 'MESSAGE'; raw: unknown} | {kind: 'TIMEOUT'; state: State<T>}>;

class Preview<T> {
  private readonly panel: PreviewPanel;
  private readonly ports: PreviewPorts<T>;

  private constructor(panel: PreviewPanel, ports: PreviewPorts<T>) {
    this.panel = panel;
    this.ports = ports;
  }

  /** Construct one panel's lifetime owner; the raw panel stays in the adapter.
   * Cost: once per panel; the initial event drains one synchronous replacement. */
  static create<T>(panel: PreviewPanel, ports: PreviewPorts<T>) {
    const preview = new Preview(panel, ports);
    preview.mailbox.post({kind: 'INITIAL_INSTALL'});
    return preview;
  }

  private state: State<T> = {id: '', phase: {kind: 'MESSAGE', text: ''}};
  private readonly mailbox = createMailbox<Event<T>>(event => this.handle(event), () => {
    /** Fault cleanup also runs after CLOSED publication fails.
     * Cost: mailbox once on fault; revoke one capability and release at most one owned timer. */
    try { this.panel.revoke(); } finally {
      const outgoing = this.state.phase;
      this.state = {id: this.state.id, phase: {kind: 'CLOSED'}};
      if (outgoing.kind === 'DISPOSING') this.ports.clock.cancel(outgoing.timer);
    }
  });

  /** Cost: command or successful build once; posts one owner event. */
  reload(): void { this.mailbox.post({kind: 'RELOAD'}); }
  /** Cost: each webview notification; posts one raw event without reading state. */
  receive(raw: unknown): void { this.mailbox.post({kind: 'MESSAGE', raw}); }
  /** Cost: native destruction once; posts one owner event. */
  close(): void { this.mailbox.post({kind: 'CLOSE'}); }

  /** Cost: mailbox once per event; own state, at most one replacement/post/timer.
   * Replacement reads one setting and checks two files; no foreign rows walked. */
  private handle(event: Event<T>): void {
    if (this.state.phase.kind === 'CLOSED') return;

    /** Cost: internal phase change; releases at most one outgoing timer. */
    const enter = (next: State<T>): void => {
      const previous = this.state.phase;
      try { this.state = next; } finally {
        if (previous.kind === 'DISPOSING') this.ports.clock.cancel(previous.timer);
      }
      const phase = next.phase;
      if (phase.kind === 'FAILED' && previous.kind !== 'FAILED') this.ports.error(phase.reason);
      this.ports.status(phase.kind === 'MESSAGE' ? phase.text
        : phase.kind === 'FAILED' ? phase.reason
        : phase.kind === 'RUNNING' ? (phase.status === 1 ? 'Windowless' : 'Running')
        : phase.kind === 'LOADING' ? 'Waiting for startup response' : phase.kind);
    };
    /** Cost: INITIAL_INSTALL or reload completion once; two files and one capability install. */
    const replace = (): 'installed' | 'closed' => {
      const admitted = this.ports.admitted();
      const folder = this.ports.folder();
      const id = this.ports.identity(), nonce = this.ports.identity();
      let input: PreviewInput;
      let roots: readonly string[] = [];
      if (!admitted || !folder) {
        input = {kind: 'message', id, nonce, cspSource: this.ports.cspSource,
          text: !admitted ? 'Trust the workspace to preview' : 'Open a workspace folder'};
      } else {
        const relative = this.ports.setting('module');
        const js = this.ports.resolve(folder, relative);
        const wasm = js.replace(/\.js$/, '.wasm');
        const jsExists = this.ports.existsSync(js), wasmExists = this.ports.existsSync(wasm);
        if (!jsExists || !wasmExists) {
          input = {kind: 'message', id, nonce, cspSource: this.ports.cspSource,
            text: 'Build the module JavaScript and Wasm files first'};
        } else {
          const source = {directory: this.ports.directory(js), jsUri: this.ports.asWebviewUri(js),
            wasmUri: this.ports.asWebviewUri(wasm), exportName: this.ports.stem(js)};
          roots = [source.directory];
          input = {kind: 'module', id, nonce, cspSource: this.ports.cspSource, ...source};
        }
      }
      const options = {enableScripts: input.kind === 'module', localResourceRoots: roots};
      const html = renderPreviewHtml(input);
      if (this.panel.install({options, html}) === 'closed') {
        enter({id: this.state.id, phase: {kind: 'CLOSED'}});
        return 'closed';
      }
      enter({id, phase: input.kind === 'message'
        ? {kind: 'MESSAGE', text: input.text} : {kind: 'LOADING'}});
      return 'installed';
    };

    switch (event.kind) {
      case 'INITIAL_INSTALL': replace(); return;
      case 'CLOSE': enter({id: this.state.id, phase: {kind: 'CLOSED'}}); return;
      case 'TIMEOUT': {
        if (this.state !== event.state) return;
        const reason = 'Previous document did not acknowledge completion or absence within 2 s; replacing';
        this.ports.output(reason);
        if (replace() === 'closed') return;
        this.ports.status(reason);
        return;
      }
      case 'RELOAD': {
        switch (this.state.phase.kind) {
          case 'DISPOSING': return;
          case 'MESSAGE': case 'ENDED': replace(); return;
          case 'LOADING': case 'RUNNING': case 'FAILED': {
            const next: State<T> = {id: this.state.id, phase: {kind: 'DISPOSING',
              timer: this.ports.clock.after(2000, () => {
                /** Cost: clock once per timer; posts one identity, reports at most one fault. */
                try { this.mailbox.post({kind: 'TIMEOUT', state: next}); }
                catch (error) { this.ports.fault(error); }
              })}};
            enter(next);
            if (this.panel.postDispose(next.id) === 'closed') {
              enter({id: next.id, phase: {kind: 'CLOSED'}});
              return;
            }
            return;
          }
        }
        return;
      }
      case 'MESSAGE': {
        const message = event.raw;
        if (!message || typeof message !== 'object') return;
        const m = message as Record<string, unknown>;
        if (typeof m.id !== 'string' || m.id !== this.state.id
          || typeof m.type !== 'string'
          || !['started', 'completed', 'absent', 'error'].includes(m.type)) return;
        if (m.type === 'started' && m.status !== 0 && m.status !== 1 && m.status !== 2) return;
        if (m.type === 'error' && typeof m.reason !== 'string') return;
        const {id, phase} = this.state;
        if (phase.kind === 'DISPOSING' && (m.type === 'completed' || m.type === 'absent')) {
          this.ports.output('Previous document: ' + m.type);
          replace();
          return;
        }
        if (phase.kind === 'LOADING' && m.type === 'started') {
          enter({id, phase: m.status === 2 ? {kind: 'FAILED', reason: 'Module start failed'}
            : {kind: 'RUNNING', status: m.status as 0 | 1}});
          return;
        }
        if (['LOADING', 'RUNNING', 'FAILED'].includes(phase.kind)) {
          if (m.type === 'completed') { enter({id, phase: {kind: 'ENDED'}}); return; }
          if (m.type === 'error') { enter({id, phase: {kind: 'FAILED', reason: m.reason as string}}); return; }
        }
        this.ports.output('Ignored ' + String(m.type) + ' in ' + phase.kind);
        return;
      }
    }
  }
}

export const createPreview = Preview.create;
