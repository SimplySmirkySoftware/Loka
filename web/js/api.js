// Captures this module forever; a disposed module never resolves to a new one.
{
  let closed=false, resolveCompleted;
  const completed=new Promise(resolve => { resolveCompleted=resolve; });
  Module['web']={
    completed,
    start: config => closed ? 2 : Module._web_start(config),
    click: token => closed ? 4 : Module._web_click(token>>>0),
    resize: (root,w,h) => closed ? 4 : Module._web_resize(root>>>0,w,h),
    wake: (kind,now) => closed ? 4 : Module._web_wake(kind,now),
    /** Roster may decline quit; keep exports open until actual completion.
     * Cost: user request once, one session entry and its App-owned admission rows. */
    quit: () => { if(!closed) Module._web_quit(); },
    /** Forced disposal bypasses the roster. Cost: once, one session lifecycle request. */
    dispose: () => { if(!closed) { closed=true; Module._web_dispose(); } },
    _complete: () => { closed=true; resolveCompleted(); }
  };
}
