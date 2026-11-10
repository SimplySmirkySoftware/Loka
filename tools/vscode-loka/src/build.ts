/** Activation-owned task ports. Execution identity belongs to the task service. */
export type BuildPorts<E extends object> = Readonly<{
  admitted(): boolean;
  execute(): PromiseLike<E>;
  reload(): void;
  report(message: string): void;
}>;
/** A task is retained only between successful acquisition and its first end. */
export type BuildRun<E> = Readonly<{kind: 'IDLE' | 'STARTING' | 'DISPOSED'}
  | {kind: 'RUNNING'; execution: E}>;

/** Construct one activation's build owner.
 * Cost: activation once; no task rows walked. */
export function createBuild<E extends object>(ports: BuildPorts<E>) {
  return new class Build {
    private run: BuildRun<E> = {kind: 'IDLE'};

    /** Cost: command once; own run, at most one task acquisition. */
    start(): void {
      if (this.run.kind === 'DISPOSED') return;
      if (this.run.kind !== 'IDLE') { ports.report('Build already active'); return; }
      if (!ports.admitted()) { ports.report('Trust the workspace to build'); return; }
      this.run = {kind: 'STARTING'};
      const failed = (error: unknown) => {
        if (this.run.kind !== 'STARTING') return;
        this.run = {kind: 'IDLE'};
        ports.report('Build failed: ' + String(error));
      };
      try {
        Promise.resolve(ports.execute()).then(execution => {
          if (this.run.kind === 'STARTING') this.run = {kind: 'RUNNING', execution};
        }, failed);
      } catch (error) { failed(error); }
    }

    /** Cost: task process listener per end; one own execution identity comparison. */
    ended(execution: E, exit: number | undefined): void {
      if (this.run.kind !== 'RUNNING' || this.run.execution !== execution) return;
      this.run = {kind: 'IDLE'};
      if (exit === 0) ports.reload();
      else ports.report('Build failed: ' + String(exit));
    }

    /** Cost: task listener per end; one own execution identity comparison. */
    taskEnded(execution: E): void {
      if (this.run.kind !== 'RUNNING' || this.run.execution !== execution) return;
      this.run = {kind: 'IDLE'};
      ports.report('Build ended without a process result');
    }

    /** Cost: deactivate once; own run only, task remains owned by the task service. */
    dispose(): void { this.run = {kind: 'DISPOSED'}; }
  }();
}
