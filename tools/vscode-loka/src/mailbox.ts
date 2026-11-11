/** Serialize one owner's events; an unexpected throw permanently closes the queue.
 * Cost: once per owner; retains only that owner's pending events. */
export function createMailbox<E>(handle: (event: E) => void, onFault: () => void): {post(event: E): void} {
  let state: {kind: 'OPEN'; queue: E[]} | {kind: 'FAULTED'} = {kind: 'OPEN', queue: []};
  return {
    /** Cost: each owner entry appends once; the empty-queue caller drains its own queue.
     * Array shifts cost the remaining queue length; nested entries only append. */
    post(event: E): void {
      if (state.kind === 'FAULTED') return;
      const queue = state.queue;
      queue.push(event);
      if (queue.length !== 1) return;
      try {
        while (queue.length) {
          handle(queue[0]);
          queue.shift();
        }
      } catch (error) {
        state = {kind: 'FAULTED'};
        queue.length = 0;
        try { onFault(); } catch { /* Cleanup cannot replace the original fault. */ }
        throw error;
      }
    },
  };
}
