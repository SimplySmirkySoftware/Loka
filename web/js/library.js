/** Module-local DOM owner. Each import addresses its own resource or timer row.
 * Buffers are consumed synchronously; callbacks retain session/token identities. */
addToLibrary({
  $WebDOM: {
    resources: new Map(), tasks: new Map(), next: 1,
    row(token) { return WebDOM.resources.get(token >>> 0); },
    /** Cost: bootstrap once per root, reads only its computed content-box dimensions.
     * Unlike getBoundingClientRect, this is unaffected by transforms. */
    contentBox(element) {
      const style = getComputedStyle(element);
      const borderBox = style.boxSizing === 'border-box';
      return [
        parseFloat(style.width) - (borderBox ? parseFloat(style.paddingLeft) + parseFloat(style.paddingRight)
          + parseFloat(style.borderLeftWidth) + parseFloat(style.borderRightWidth) : 0),
        parseFloat(style.height) - (borderBox ? parseFloat(style.paddingTop) + parseFloat(style.paddingBottom)
          + parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth) : 0)
      ];
    },
    hidden(row, value) {
      row.element.style.setProperty('display', value ? 'none' : (row.kind === 0 ? 'block' : 'inline-block'), 'important');
      row.element.inert = !!value;
    },
    arm(callback, owner, kind, ms) {
      const handle = WebDOM.next++;
      const run = () => {
        if (!WebDOM.tasks.has(handle)) return;
        WebDOM.tasks.delete(handle);
        getWasmTableEntry(callback)(owner, handle, kind, performance.now());
      };
      const native = kind === 0 ? requestAnimationFrame(run) : setTimeout(run, ms);
      WebDOM.tasks.set(handle, {native, kind});
      return handle;
    }
  },
  web_create__deps: ['$WebDOM'],
  web_create(kind, token, parent, width, height) {
    token >>>= 0; parent >>>= 0;
    let element, row;
    try {
      element = document.createElement(kind === 2 ? 'button' : kind === 1 ? 'span' : 'div');
      row = {element, kind, listener:null, observer:null};
      element.dataset.lokaToken = String(token);
      element.style.cssText = 'box-sizing:border-box;position:absolute;white-space:pre;visibility:hidden;';
      if (kind === 0) {
        element.style.position = 'relative';
        element.style.width = HEAP32[width>>2]+'px';
        element.style.height = HEAP32[height>>2]+'px';
        element.style.maxWidth = '100%';
        document.body.appendChild(element);
        // Same content box as ResizeObserver.contentRect, before its first asynchronous delivery.
        const content = WebDOM.contentBox(element);
        HEAP32[width>>2] = Math.round(content[0]);
        HEAP32[height>>2] = Math.round(content[1]);
        row.observer = new ResizeObserver(entries => {
          for (const entry of entries) {
            if (!WebDOM.resources.has(token) || element.style.display === 'none') continue;
            Module['web'].resize(token, Math.round(entry.contentRect.width), Math.round(entry.contentRect.height));
          }
        });
        row.observer.observe(element);
      } else {
        const root = WebDOM.row(parent);
        if (!root) throw new Error('missing root');
        root.element.appendChild(element);
        if (kind === 2) {
          element.type = 'button';
          row.listener = () => Module['web'].click(token);
          element.addEventListener('click', row.listener);
        }
      }
      WebDOM.hidden(row, true);
      element.style.visibility = 'visible';
      WebDOM.resources.set(token, row);
      return 1;
    } catch (_) {
      if (row && row.observer) row.observer.disconnect();
      if (row && row.listener) element.removeEventListener('click', row.listener);
      if (element) element.remove();
      return 0;
    }
  },
  web_set_rect__deps:['$WebDOM'],
  web_set_rect(token,x,y,w,h) {
    const row=WebDOM.row(token); if(!row) return;
    Object.assign(row.element.style,{left:x+'px',top:y+'px',width:w+'px',height:h+'px'});
  },
  web_set_text__deps:['$WebDOM','$UTF8ToString'],
  web_set_text(token,bytes,length) { const row=WebDOM.row(token); if(row) row.element.textContent=UTF8ToString(bytes,length); },
  web_set_text_style__deps:['$WebDOM'],
  web_set_text_style(token,descriptor,count) {
    const row=WebDOM.row(token); if(!row || count!==10) return;
    const d=HEAP32.subarray(descriptor>>2,(descriptor>>2)+count);
    if(d[0]!==1 || d[1]!==10) return;
    const s=row.element.style;
    // PR 1 fixed metrics. Page 3 replaces font resolution and geometry together.
    s.fontSize='16px'; s.lineHeight='16px';
    s.fontFamily=d[7]===LokaWebStyle.family.MONOSPACE ? 'monospace' : 'system-ui';
    s.fontWeight=d[4]===LokaWebStyle.weights.BOLD ? 'bold' : 'normal';
    s.fontStyle=d[5]===1 ? 'italic':'normal';
    s.textDecoration=d[8]===1 ? 'underline':'none';
    const colors=LokaWebStyle.colors;
    const palette={ [colors.UNSET]:'CanvasText', [colors.PRIMARY]:'CanvasText',
      [colors.SECONDARY]:'GrayText', [colors.BLACK]:'black', [colors.WHITE]:'white',
      [colors.RED]:'red', [colors.GREEN]:'green', [colors.BLUE]:'blue',
      [colors.CYAN]:'cyan', [colors.MAGENTA]:'magenta', [colors.YELLOW]:'yellow' };
    s.color=palette[d[6]] || 'CanvasText';
  },
  web_set_enabled__deps:['$WebDOM'],
  web_set_enabled(token,value) { const row=WebDOM.row(token); if(row) row.element.disabled=!value; },
  web_set_hidden__deps:['$WebDOM'],
  web_set_hidden(token,value) { const row=WebDOM.row(token); if(!row) return 0; WebDOM.hidden(row,value); return 1; },
  web_revoke__deps:['$WebDOM'],
  web_revoke(token) {
    const row=WebDOM.row(token); if(!row) return;
    if(row.listener) row.element.removeEventListener('click',row.listener);
    if(row.observer) row.observer.disconnect();
    row.listener=row.observer=null;
  },
  web_release__deps:['$WebDOM'],
  web_release(token) {
    token >>>=0; if(!token) return 2;
    const row=WebDOM.row(token); if(!row) return 1;
    if(row.listener) row.element.removeEventListener('click',row.listener);
    if(row.observer) row.observer.disconnect();
    row.element.remove(); WebDOM.resources.delete(token); return 0;
  },
  web_arm_frame__deps:['$WebDOM','$getWasmTableEntry'],
  web_arm_frame(callback,owner) { return WebDOM.arm(callback,owner,0,0); },
  web_arm_task__deps:['$WebDOM','$getWasmTableEntry'],
  web_arm_task(callback,owner,kind,ms) { return WebDOM.arm(callback,owner,kind,ms); },
  web_cancel__deps:['$WebDOM'],
  web_cancel(handle) {
    const row=WebDOM.tasks.get(handle); if(!row) return;
    if(row.kind===0) cancelAnimationFrame(row.native); else clearTimeout(row.native);
    WebDOM.tasks.delete(handle);
  },
  web_read_native_focus__deps:['$WebDOM'],
  web_read_native_focus(root) {
    const row=WebDOM.row(root), active=document.activeElement;
    return row && row.element.contains(active) ? Number(active.dataset.lokaToken || 0)>>>0 : 0;
  },
  web_completed__deps:['$WebDOM'],
  web_completed() {
    if(WebDOM.resources.size) throw new Error('undischarged Web resources');
    Module['web']._complete();
  }
});
