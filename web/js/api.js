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
    quit: () => { if(!closed) { closed=true; Module._web_quit(); } },
    dispose: () => { if(!closed) { closed=true; Module._web_dispose(); } },
    _complete: () => { closed=true; resolveCompleted(); }
  };
}
