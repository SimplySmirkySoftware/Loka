/** Activation-owned task ports. Execution identity belongs to the task service. */
export type BuildPorts<E extends object> = Readonly<{
  admitted(): boolean;
  execute(): PromiseLike<E>;
  reload(): void;
  report(message: string): void;
}>;
/** Acquisition owns early end facts; the acquired identity selects its outcome. */
export type BuildRun<E> = Readonly<{kind: 'IDLE' | 'DISPOSED'}
  | {kind: 'STARTING'; early: ReadonlyArray<Readonly<{execution: E; exit: number | undefined; process: boolean}>>}
  | {kind: 'RUNNING'; execution: E}>;

/** Construct one activation's build owner.
 * Cost: activation once; no task rows walked. */
export function createBuild<E extends object>(ports: BuildPorts<E>) {
  return new class Build {
    private run: BuildRun<E> = {kind: 'IDLE'};

    /** Cost: command once; own run, one acquisition; resolution scans retained early facts at most twice. */
    start(): void {
      if (this.run.kind === 'DISPOSED') return;
      if (this.run.kind !== 'IDLE') { ports.report('Build already active'); return; }
      if (!ports.admitted()) { ports.report('Trust the workspace to build'); return; }
      this.run = {kind: 'STARTING', early: []};
      const failed = (error: unknown) => {
        if (this.run.kind !== 'STARTING') return;
        this.run = {kind: 'IDLE'};
        ports.report('Build failed: ' + String(error));
      };
      try {
        Promise.resolve(ports.execute()).then(execution => {
          if (this.run.kind !== 'STARTING') return;
          const early = this.run.early;
          const end = early.find(fact => fact.execution === execution && fact.process)
            ?? early.find(fact => fact.execution === execution);
          this.run = {kind: 'RUNNING', execution};
          if (end) {
            if (end.process) this.ended(execution, end.exit);
            else this.taskEnded(execution);
          }
        }, failed);
      } catch (error) { failed(error); }
    }

    /** Cost: task process listener per end; one identity comparison when RUNNING; STARTING copies its early facts once. */
    ended(execution: E, exit: number | undefined): void {
      if (this.run.kind === 'STARTING') {
        this.run = {kind: 'STARTING', early: [...this.run.early, {execution, exit, process: true}]};
        return;
      }
      if (this.run.kind !== 'RUNNING' || this.run.execution !== execution) return;
      this.run = {kind: 'IDLE'};
      if (exit === 0) ports.reload();
      else ports.report('Build failed: ' + String(exit));
    }

    /** Cost: task listener per end; one identity comparison when RUNNING; STARTING copies its early facts once. */
    taskEnded(execution: E): void {
      if (this.run.kind === 'STARTING') {
        this.run = {kind: 'STARTING', early: [...this.run.early, {execution, exit: undefined, process: false}]};
        return;
      }
      if (this.run.kind !== 'RUNNING' || this.run.execution !== execution) return;
      this.run = {kind: 'IDLE'};
      ports.report('Build ended without a process result');
    }

    /** Cost: deactivate once; own run only, task remains owned by the task service. */
    dispose(): void { this.run = {kind: 'DISPOSED'}; }
  }();
}
