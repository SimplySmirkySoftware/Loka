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

type Event<T> = Readonly<{kind: 'OPEN' | 'RELOAD' | 'CLOSE'}
  | {kind: 'MESSAGE'; raw: unknown} | {kind: 'TIMEOUT'; state: State<T>}>;

/** Construct one panel's lifetime owner; the raw panel stays in the adapter.
 * Cost: once per panel; OPEN drains one synchronous replacement. */
export function createPreview<T>(panel: PreviewPanel, ports: PreviewPorts<T>) {
  class Preview {
    private state: State<T> = {id: '', phase: {kind: 'MESSAGE', text: ''}};
    private readonly mailbox = createMailbox<Event<T>>(event => this.handle(event), () => {
      /** Fault cleanup also runs after CLOSED publication fails.
       * Cost: mailbox once on fault; revoke one capability and release at most one owned timer. */
      try { panel.revoke(); } finally {
        const outgoing = this.state.phase;
        this.state = {id: this.state.id, phase: {kind: 'CLOSED'}};
        if (outgoing.kind === 'DISPOSING') ports.clock.cancel(outgoing.timer);
      }
    });

    /** Cost: public open once per panel; posts one owner event. */
    open(): void { this.mailbox.post({kind: 'OPEN'}); }
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
          if (previous.kind === 'DISPOSING') ports.clock.cancel(previous.timer);
        }
        const phase = next.phase;
        if (phase.kind === 'FAILED' && previous.kind !== 'FAILED') ports.error(phase.reason);
        ports.status(phase.kind === 'MESSAGE' ? phase.text
          : phase.kind === 'FAILED' ? phase.reason
          : phase.kind === 'RUNNING' ? (phase.status === 1 ? 'Windowless' : 'Running')
          : phase.kind === 'LOADING' ? 'Waiting for startup response' : phase.kind);
      };
      /** Cost: OPEN or reload completion once; two files and one capability install. */
      const replace = (): 'installed' | 'closed' => {
        const admitted = ports.admitted();
        const folder = ports.folder();
        const id = ports.identity(), nonce = ports.identity();
        let input: PreviewInput;
        let roots: readonly string[] = [];
        if (!admitted || !folder) {
          input = {kind: 'message', id, nonce, cspSource: ports.cspSource,
            text: !admitted ? 'Trust the workspace to preview' : 'Open a workspace folder'};
        } else {
          const relative = ports.setting('module');
          const js = ports.resolve(folder, relative);
          const wasm = js.replace(/\.js$/, '.wasm');
          const jsExists = ports.existsSync(js), wasmExists = ports.existsSync(wasm);
          if (!jsExists || !wasmExists) {
            input = {kind: 'message', id, nonce, cspSource: ports.cspSource,
              text: 'Build the module JavaScript and Wasm files first'};
          } else {
            const source = {directory: ports.directory(js), jsUri: ports.asWebviewUri(js),
              wasmUri: ports.asWebviewUri(wasm), exportName: ports.stem(js)};
            roots = [source.directory];
            input = {kind: 'module', id, nonce, cspSource: ports.cspSource, ...source};
          }
        }
        const options = {enableScripts: input.kind === 'module', localResourceRoots: roots};
        const html = renderPreviewHtml(input);
        if (panel.install({options, html}) === 'closed') {
          enter({id: this.state.id, phase: {kind: 'CLOSED'}});
          return 'closed';
        }
        enter({id, phase: input.kind === 'message'
          ? {kind: 'MESSAGE', text: input.text} : {kind: 'LOADING'}});
        return 'installed';
      };

      switch (event.kind) {
        case 'OPEN': replace(); return;
        case 'CLOSE': enter({id: this.state.id, phase: {kind: 'CLOSED'}}); return;
        case 'TIMEOUT': {
          if (this.state !== event.state) return;
          const reason = 'Previous document did not acknowledge completion or absence within 2 s; replacing';
          ports.output(reason);
          if (replace() === 'closed') return;
          ports.status(reason);
          return;
        }
        case 'RELOAD': {
          switch (this.state.phase.kind) {
            case 'DISPOSING': return;
            case 'MESSAGE': case 'ENDED': replace(); return;
            case 'LOADING': case 'RUNNING': case 'FAILED': {
              const next: State<T> = {id: this.state.id, phase: {kind: 'DISPOSING',
                timer: ports.clock.after(2000, () => {
                  /** Cost: clock once per timer; posts one identity, reports at most one fault. */
                  try { this.mailbox.post({kind: 'TIMEOUT', state: next}); }
                  catch (error) { ports.fault(error); }
                })}};
              enter(next);
              if (panel.postDispose(next.id) === 'closed') {
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
            ports.output('Previous document: ' + m.type);
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
          ports.output('Ignored ' + String(m.type) + ' in ' + phase.kind);
          return;
        }
      }
    }
  }
  const preview = new Preview();
  preview.open();
  return preview;
}
