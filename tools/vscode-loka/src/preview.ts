import {renderPreviewHtml} from './previewHtml.ts';
import type {PreviewInput} from './previewHtml.ts';

/** Host panel boundary; options and HTML are assigned synchronously in that order. */
export type PreviewPanel = {
  options: {enableScripts: boolean; localResourceRoots: readonly string[]};
  html: string;
  postMessage(message: Readonly<{id: string; type: 'dispose'}>): void;
};
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
}>;
type Phase<T> = Readonly<
  {kind: 'MESSAGE'; text: string} | {kind: 'LOADING'}
  | {kind: 'RUNNING'; status: 0 | 1} | {kind: 'FAILED'; reason: string}
  | {kind: 'ENDED'} | {kind: 'DISPOSING'; timer: T} | {kind: 'CLOSED'}>;
type State<T> = Readonly<{id: string; phase: Phase<T>}>;

/** Construct one panel's lifetime owner. Ports live in the creation scope.
 * Cost: open once; one synchronous replacement of this panel. */
export function createPreview<T>(panel: PreviewPanel, ports: PreviewPorts<T>) {
  class Preview {
    readonly panel: PreviewPanel;
    private state!: State<T>;

    constructor() {
      this.panel = panel;
      this.enter({id: '', phase: {kind: 'MESSAGE', text: ''}});
      this.replace();
    }

    /** Cost: internal transition once; own state and at most one owned timer. */
    private enter(next: State<T>): void {
      if (this.state?.phase.kind === 'DISPOSING') ports.clock.cancel(this.state.phase.timer);
      const previous = this.state?.phase;
      this.state = next;
      const phase = next.phase;
      if (phase.kind === 'FAILED' && previous?.kind !== 'FAILED') ports.error(phase.reason);
      ports.status(phase.kind === 'MESSAGE' ? phase.text
        : phase.kind === 'FAILED' ? phase.reason
        : phase.kind === 'RUNNING' ? (phase.status === 1 ? 'Windowless' : 'Running')
        : phase.kind === 'LOADING' ? 'Waiting for startup response' : phase.kind);
    }

    /** Cost: open or reload completion once; one setting, two existence checks, own panel only. */
    private replace(): void {
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
      this.panel.options = options;
      this.panel.html = html;
      this.enter({id, phase: input.kind === 'message'
        ? {kind: 'MESSAGE', text: input.text} : {kind: 'LOADING'}});
    }

    /** Cost: command or successful build once; own state, at most one post and timer. */
    reload(): void {
      switch (this.state.phase.kind) {
        case 'CLOSED': case 'DISPOSING': return;
        case 'MESSAGE': case 'ENDED': this.replace(); return;
        case 'LOADING': case 'RUNNING': case 'FAILED': {
          const next: State<T> = {id: this.state.id, phase: {kind: 'DISPOSING',
            timer: ports.clock.after(2000, () => {
              if (this.state !== next) return;
              const reason = 'Previous document did not acknowledge completion or absence within 2 s; replacing';
              ports.output(reason);
              this.replace();
              ports.status(reason);
            })}};
          this.enter(next);
          this.panel.postMessage({id: next.id, type: 'dispose'});
        }
      }
    }

    /** Cost: webview event; own state only, one replacement on disposal acknowledgement. */
    receive(message: unknown): void {
      if (!message || typeof message !== 'object') return;
      const m = message as Record<string, unknown>;
      if (typeof m.id !== 'string' || m.id !== this.state.id
        || typeof m.type !== 'string'
        || !['started', 'completed', 'absent', 'error'].includes(m.type)) return;
      if (m.type === 'started' && m.status !== 0 && m.status !== 1 && m.status !== 2) return;
      if (m.type === 'error' && typeof m.reason !== 'string') return;
      const {id, phase} = this.state;
      if (phase.kind === 'CLOSED') return;
      if (phase.kind === 'DISPOSING' && (m.type === 'completed' || m.type === 'absent')) {
        ports.output('Previous document: ' + m.type);
        this.replace();
        return;
      }
      if (phase.kind === 'LOADING' && m.type === 'started') {
        this.enter({id, phase: m.status === 2 ? {kind: 'FAILED', reason: 'Module start failed'}
          : {kind: 'RUNNING', status: m.status as 0 | 1}});
        return;
      }
      if (['LOADING', 'RUNNING', 'FAILED'].includes(phase.kind)) {
        if (m.type === 'completed') { this.enter({id, phase: {kind: 'ENDED'}}); return; }
        if (m.type === 'error') {
          this.enter({id, phase: {kind: 'FAILED', reason: m.reason as string}});
          return;
        }
      }
      ports.output('Ignored ' + String(m.type) + ' in ' + phase.kind);
    }

    /** Cost: panel destruction once; cancels own timer, no page disposal after destruction. */
    close(): void { this.enter({id: this.state.id, phase: {kind: 'CLOSED'}}); }
  }
  return new Preview();
}
