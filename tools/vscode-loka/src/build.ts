import {createMailbox} from './mailbox.ts';

/** Immutable admitted task input, retained through acquisition and completion. */
export type Operation = Readonly<{cwd: string} & ({kind: 'CONFIGURE'; command: string}
  | {kind: 'BUILD'; directory: string; target: string})>;

/** Activation-owned task ports. Execution identity belongs to the task service. */
export type BuildPorts<E extends object> = Readonly<{
  admitted(): boolean;
  /** Cost: idle trusted start once; folder admission and one completed operation or diagnostic. */
  prepare(kind: Operation['kind']): Operation | Readonly<{message: string}>;
  execute(operation: Operation): PromiseLike<E>;
  reload(): void;
  report(message: string): void;
  /** Nonthrowing asynchronous fault sink. Cost: once per failed settlement drain. */
  fault(error: unknown): void;
}>;
/** Acquisition owns early end facts; the acquired identity selects its outcome. */
export type BuildRun<E> = Readonly<{kind: 'IDLE' | 'DISPOSED'}
  | {kind: 'STARTING'; operation: Operation; early: ReadonlyArray<Readonly<{execution: E; exit: number | undefined; process: boolean}>>}
  | {kind: 'RUNNING'; operation: Operation; execution: E}>;
type Event<E> = Readonly<{kind: 'DISPOSE'} | {kind: 'START'; operationKind: Operation['kind']} | {kind: 'ACQUIRED'; execution: E}
  | {kind: 'REJECTED'; error: unknown} | {kind: 'PROCESS_ENDED'; execution: E; exit: number | undefined}
  | {kind: 'TASK_ENDED'; execution: E}>;

/** Construct one activation's build owner.
 * Cost: activation once; no task rows walked. */
export function createBuild<E extends object>(ports: BuildPorts<E>) {
  return new class Build {
    private run: BuildRun<E> = {kind: 'IDLE'};
    private readonly mailbox = createMailbox<Event<E>>(event => this.handle(event), () => {});

    /** Cost: command once; posts one owner event. */
    start(operationKind: Operation['kind'] = 'BUILD'): void { this.mailbox.post({kind: 'START', operationKind}); }
    /** Cost: process listener per end; posts one identity/result pair. */
    ended(execution: E, exit: number | undefined): void { this.mailbox.post({kind: 'PROCESS_ENDED', execution, exit}); }
    /** Cost: task listener per end; posts one execution identity. */
    taskEnded(execution: E): void { this.mailbox.post({kind: 'TASK_ENDED', execution}); }
    /** Cost: deactivate once; posts one event, task remains owned by the service. */
    dispose(): void { this.mailbox.post({kind: 'DISPOSE'}); }

    /** Cost: mailbox once per event; own run only. STARTING copies its early facts
     * per end; ACQUIRED scans those facts at most twice, with no foreign task walk. */
    private handle(event: Event<E>): void {
      if (this.run.kind === 'DISPOSED') return;
      /** Cost: one reconciled or running end; one result publication. */
      const finish = (process: boolean, exit: number | undefined): void => {
        if (this.run.kind !== 'RUNNING') return;
        const operation = this.run.operation;
        this.run = {kind: 'IDLE'};
        if (!process) ports.report('Build ended without a process result');
        else if (exit === 0) {
          if (operation.kind === 'CONFIGURE') ports.report('Configured');
          else { ports.report('Built ' + operation.target); ports.reload(); }
        }
        else ports.report('Build failed: ' + String(exit));
      };
      switch (event.kind) {
        case 'START': {
          if (this.run.kind !== 'IDLE') { ports.report('Build already active'); return; }
          if (!ports.admitted()) { ports.report('Trust the workspace to build'); return; }
          const operation = ports.prepare(event.operationKind);
          if ('message' in operation) { ports.report(operation.message); return; }
          this.run = {kind: 'STARTING', operation, early: []};
          let pending: PromiseLike<E>;
          try { pending = ports.execute(operation); }
          catch (error) { this.mailbox.post({kind: 'REJECTED', error}); return; }
          /** Cost: one settlement pair per execution; each callback posts once and reports at most one fault. */
          Promise.resolve(pending).then(execution => {
            try { this.mailbox.post({kind: 'ACQUIRED', execution}); }
            catch (error) { ports.fault(error); }
          }, error => {
            try { this.mailbox.post({kind: 'REJECTED', error}); }
            catch (fault) { ports.fault(fault); }
          });
          return;
        }
        case 'ACQUIRED': {
          if (this.run.kind !== 'STARTING') return;
          const early = this.run.early;
          const end = early.find(fact => fact.execution === event.execution && fact.process)
            ?? early.find(fact => fact.execution === event.execution);
          this.run = {kind: 'RUNNING', operation: this.run.operation, execution: event.execution};
          if (end) finish(end.process, end.exit);
          return;
        }
        case 'REJECTED':
          if (this.run.kind !== 'STARTING') return;
          this.run = {kind: 'IDLE'};
          ports.report('Build failed: ' + String(event.error));
          return;
        case 'PROCESS_ENDED': case 'TASK_ENDED': {
          const process = event.kind === 'PROCESS_ENDED';
          const exit = event.kind === 'PROCESS_ENDED' ? event.exit : undefined;
          if (this.run.kind === 'STARTING') {
            this.run = {kind: 'STARTING', operation: this.run.operation, early: [...this.run.early, {execution: event.execution, exit, process}]};
            return;
          }
          if (this.run.kind !== 'RUNNING' || this.run.execution !== event.execution) return;
          finish(process, exit);
          return;
        }
        case 'DISPOSE': this.run = {kind: 'DISPOSED'}; return;
      }
    }
  }();
}
