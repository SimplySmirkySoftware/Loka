/** Module-local DOM owner. Each import addresses its own resource or timer row.
 * Buffers are consumed synchronously; callbacks retain session/token identities. */
addToLibrary({
  /** Cost: bootstrap once after admissions; resolves the current module option, no resource rows. */
  web_skin__deps: ['$WebDOM'],
  web_skin: () => WebDOM.skin = LokaWebStyle.resolveSkin(Module['lokaSkin']),
  $WebDOM: {
    text: null, skin: 0,
    resources: new Map(), tasks: new Map(), next: 1,
    row(token) { return WebDOM.resources.get(token >>> 0); },
    parent(token) { const row = WebDOM.row(token); return row && (row.kind === 3 ? row.content : row.element); },
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
      element = document.createElement((kind === 2 || kind === 4) ? 'button' : kind === 1 ? 'span' : 'div');
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
        const root = WebDOM.parent(parent);
        if (!root) throw new Error('missing root');
        root.appendChild(element);
        if (kind === 3) {
          const physical = 'box-sizing:border-box;margin:0;padding:0;border:0;min-width:0;min-height:0;direction:ltr;writing-mode:horizontal-tb;';
          element.style.cssText = physical + 'position:absolute;overflow-y:scroll;overflow-x:hidden;';
          row.content = document.createElement('div');
          row.content.style.cssText = physical + 'position:relative;overflow:clip;';
          element.appendChild(row.content);
          row.listener = () => Module['_web_scroll'](token, Math.round(element.scrollLeft), Math.round(element.scrollTop));
          element.addEventListener('scroll', row.listener, {passive:true});
        }
        if (kind === 2 || kind === 4) {
          element.type = 'button';
          const g=LokaWebStyle.webButtonGeometry;
          Object.assign(element.style,{appearance:'none',boxSizing:'border-box',
            padding:g.insetY+'px '+g.insetX+'px',borderWidth:g.border+'px',borderStyle:'solid',
            margin:'0',minWidth:'0',minHeight:'0',whiteSpace:'pre',overflow:'hidden'});
          row.listener = () => Module['web'].click(token);
          element.addEventListener('click', row.listener);
        }
      }
      // Cost: creation once; at most four properties on this owned element, no resource walk.
      const look=LokaWebStyle.skinLook[WebDOM.skin];
      const paint=kind===0 ? look.root : kind===2 ? look.button : kind===4 ? look.cell : {};
      for (const property of Object.keys(paint)) element.style.setProperty(property,paint[property]);
      WebDOM.hidden(row, true);
      element.style.visibility = 'visible';
      WebDOM.resources.set(token, row);
      return 1;
    } catch (_) {
      if (row && row.observer) row.observer.disconnect();
      if (row && row.listener) element.removeEventListener(row.kind === 3 ? 'scroll' : 'click', row.listener);
      if (row && row.content) row.content.remove();
      if (element) element.remove();
      return 0;
    }
  },
  web_commit_scroll__deps:['$WebDOM'],
  web_commit_scroll(token,x,y,w,h,horizontal,documentWidth,extent,originX,originY,appliedX,appliedY) {
    const row=WebDOM.row(token);
    if(!row || row.kind!==3) return 2;
    const element=row.element;
    // getClientRects is empty for display:none, including an ancestor's display:none;
    // a zero-sized but laid-out box still has a rect and is eligible.
    if(!element.getClientRects().length) return 1;
    Object.assign(element.style,{left:x+'px',top:y+'px',width:w+'px',height:h+'px'});
    element.style.overflowX=horizontal ? 'scroll' : 'hidden';
    Object.assign(row.content.style,{width:documentWidth+'px',height:extent+'px'});
    if(Math.round(element.scrollLeft)!==originX) element.scrollLeft=originX;
    if(Math.round(element.scrollTop)!==originY) element.scrollTop=originY;
    const ax=Math.round(element.scrollLeft), ay=Math.round(element.scrollTop);
    if(!Number.isFinite(ax) || !Number.isFinite(ay)) return 2;
    HEAP32[appliedX>>2]=ax; HEAP32[appliedY>>2]=ay;
    return 0;
  },
  web_scrollbar_geometry(vertical,horizontal) {
    const probe=document.createElement('div');
    probe.style.cssText='position:fixed;left:0;top:0;width:100px;height:100px;overflow:scroll;visibility:hidden;pointer-events:none;margin:0;padding:0;border:0;direction:ltr;writing-mode:horizontal-tb;';
    try {
      document.body.appendChild(probe);
      if(!probe.getClientRects().length || probe.offsetWidth!==100 || probe.offsetHeight!==100) return 0;
      const v=probe.offsetWidth-probe.clientWidth, h=probe.offsetHeight-probe.clientHeight;
      HEAP32[vertical>>2]=v; HEAP32[horizontal>>2]=h;
      return 1;
    } finally { probe.remove(); }
  },
  web_set_rect__deps:['$WebDOM'],
  web_set_rect(token,x,y,w,h) {
    const row=WebDOM.row(token); if(!row) return;
    Object.assign(row.element.style,{left:x+'px',top:y+'px',width:w+'px',height:h+'px'});
  },
  web_set_projected__deps:['$WebDOM'],
  /** Cost: render once per visit, writes one owned element. */
  web_set_projected(token,projected) { const row=WebDOM.row(token); if(row) row.element.style.visibility=projected ? 'visible' : 'hidden'; },
  web_set_text__deps:['$WebDOM'],
  web_set_text(token,bytes,length) { const row=WebDOM.row(token); if(row) row.element.textContent=new TextDecoder('utf-8',{ignoreBOM:true}).decode(HEAPU8.subarray(bytes>>>0,(bytes>>>0)+length)); },
  web_set_line_height__deps:['$WebDOM'],
  /** Cost: render once per visit, one element write after font. */
  web_set_line_height(token,height) { const row=WebDOM.row(token); if(row) row.element.style.lineHeight=height+'px'; },
  web_set_block__deps:['$WebDOM'],
  /** Cost: render once per visit, three properties of its own element. */
  web_set_block(token,align,truncation) {
    const row=WebDOM.row(token); if(!row) return;
    Object.assign(row.element.style,{textAlign:['left','center','right'][align],
      textOverflow:truncation===LokaWebStyle.truncation.ELLIPSIS?'ellipsis':'clip',overflow:'hidden'});
  },
  web_measure__deps:['$WebDOM'],
  /** Cost: range miss or natural query once; exact borrowed bytes, one canvas measure. */
  web_measure(descriptor,ptr,len,out) {
    const status=LokaWebStyle.webMeasureStatus;
    try {
      const d=LokaWebStyle.decodeTextDescriptor(HEAP32,descriptor>>>0,LokaWebStyle.wire.count);
      if(!d) return status.PROTOCOL;
      const service=WebDOM.text;
      if(!service || service.ctx.isContextLost()) return status.REFUSED;
      ptr>>>=0; len>>>=0;
      if(ptr>HEAPU8.length || len>HEAPU8.length-ptr) return status.PROTOCOL;
      service.environment(LokaWebStyle.fontKey(d)).applyToCanvas(service.ctx);
      const raw=service.ctx.measureText(new TextDecoder('utf-8',{ignoreBOM:true}).decode(HEAPU8.subarray(ptr,ptr+len))).width;
      const width=Math.ceil(raw);
      if(!Number.isFinite(raw) || raw<0 || width>2147483647) return status.REFUSED;
      HEAP32[out>>2]=width; return status.OK;
    } catch (_) { return status.REFUSED; }
  },
  web_metrics__deps:['$WebDOM'],
  /** Cost: build/query once; service memo measures at most once per font key. */
  web_metrics(descriptor,ascent,descent) {
    const status=LokaWebStyle.webMeasureStatus;
    try {
      const d=LokaWebStyle.decodeTextDescriptor(HEAP32,descriptor>>>0,LokaWebStyle.wire.count);
      if(!d) return status.PROTOCOL;
      const service=WebDOM.text;
      if(!service || service.ctx.isContextLost()) return status.REFUSED;
      const metrics=service.metrics(LokaWebStyle.fontKey(d));
      if(!metrics) return status.REFUSED;
      HEAP32[ascent>>2]=metrics.ascent; HEAP32[descent>>2]=metrics.descent;
      return status.OK;
    } catch (_) { return status.REFUSED; }
  },
  web_text_admit__deps:['$WebDOM'],
  /** Cost: session start once; publish only a fully admitted private service. */
  web_text_admit(fingerprint,count) {
    const service=LokaWebText.admit(fingerprint,count,document,navigator,globalThis.CSS,
      message => console.error(message));
    if (!service) return false;
    WebDOM.text=service;
    return true;
  },
  web_set_text_style__deps:['$WebDOM'],
  /** Cost: Text render once per visit; nine borrowed words and one element's Env rows. */
  web_set_text_style(token,descriptor,count) {
    const d=LokaWebStyle.decodeTextDescriptor(HEAP32,descriptor>>>0,count);
    if (!d || !WebDOM.text) return false; // protocol refusal, not a lifecycle miss
    const row=WebDOM.row(token); if(!row) return true; // released token: a no-op like web_set_text
    WebDOM.text.environment(LokaWebStyle.fontKey(d)).applyToElement(row.element);
    const s=row.element.style;
    s.textDecoration=d.underline===1 ? 'underline':'none';
    const colors=LokaWebStyle.colors;
    const palette={ [colors.UNSET]:'var(--loka-text)', [colors.PRIMARY]:'var(--loka-text)',
      [colors.SECONDARY]:'var(--loka-secondary)', [colors.BLACK]:'black', [colors.WHITE]:'white',
      [colors.RED]:'red', [colors.GREEN]:'green', [colors.BLUE]:'blue',
      [colors.CYAN]:'cyan', [colors.MAGENTA]:'magenta', [colors.YELLOW]:'yellow' };
    s.color=palette[d.color];
    return true;
  },
  web_set_enabled__deps:['$WebDOM'],
  /** Cost: each Button/Cell render; one owned element, no resource walk or remembered flag. */
  web_set_enabled(token,value) {
    const row=WebDOM.row(token); if(!row) return;
    row.element.disabled=!value;
    if(value) row.element.style.removeProperty('--loka-text');
    else row.element.style.setProperty('--loka-text',LokaWebStyle.skinLook[WebDOM.skin].disabledText);
  },
  web_set_hidden__deps:['$WebDOM'],
  web_set_hidden(token,value) { const row=WebDOM.row(token); if(!row) return 0; WebDOM.hidden(row,value); return 1; },
  web_revoke__deps:['$WebDOM'],
  web_revoke(token) {
    const row=WebDOM.row(token); if(!row) return;
    if(row.listener) row.element.removeEventListener(row.kind === 3 ? 'scroll' : 'click',row.listener);
    if(row.observer) row.observer.disconnect();
    row.listener=row.observer=null;
  },
  web_release__deps:['$WebDOM'],
  web_release(token) {
    token >>>=0; if(!token) return 2;
    const row=WebDOM.row(token); if(!row) return 1;
    if(row.listener) row.element.removeEventListener(row.kind === 3 ? 'scroll' : 'click',row.listener);
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
  /** Cost: completion once after native drain; drops this session's canvas/context and <=48 Env values. */
  web_completed() {
    if(WebDOM.resources.size) throw new Error('undischarged Web resources');
    WebDOM.text=null;
    Module['web']._complete();
  }
});
