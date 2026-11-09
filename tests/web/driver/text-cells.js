/** Browser-only cells. Served by the driver; no test access is shipped in the module. */
window.runTextCells = async function() {
  const check = (condition, message) => { if (!condition) throw new Error(message); };
  const normal = fixture.textTest;
  check(normal.service() !== null, 'admitted service');
  check(!normal.service().canvas.isConnected, 'canvas stays detached');
  const face = new FontFace('Loka Text Probe', 'url(text-probe.ttf)');
  document.fonts.add(await face.load());
  const widths = [];
  for (const lang of ['ja', 'zh', 'tr']) {
    document.documentElement.lang = lang;
    const service = normal.admit();
    check(service && service.lang === lang, 'language admission '+lang);
    const env = service.environment(16);
    document.documentElement.lang = 'en';
    const fresh = service.environment(8);
    fresh.applyToCanvas(service.ctx);
    check(service.ctx.lang === lang, 'new key retains admitted language');
    for (const polluted of [false, true]) {
      const parent = document.createElement('div');
      if (polluted) parent.style.cssText = 'text-transform:uppercase;letter-spacing:2px;font-synthesis:none;text-autospace:normal;direction:rtl;';
      const el = document.createElement('span'); el.textContent = 'aaaa';
      parent.appendChild(el); document.body.appendChild(parent);
      for (const key of [16, 20]) {
        const resolved = service.environment(key);
        resolved.applyToCanvas(service.ctx); resolved.applyToElement(el);
        // Fixture substitutes its original regular-only face on BOTH targets after Env application.
        service.ctx.font = service.ctx.font.replace('system-ui', '"Loka Text Probe"');
        el.style.fontFamily = '"Loka Text Probe"';
        const advance = service.ctx.measureText(el.textContent).width;
        const range = document.createRange(); range.selectNodeContents(el);
        check(Math.abs(range.getBoundingClientRect().width-advance)<1, 'canvas/element advance '+lang);
        if (!polluted && key === 16) widths.push(advance);
        check(el.lang === lang, 'element fixed language');
      }
      parent.remove();
    }
    check(service.environment(16) === env, 'Env identity stable');
  }
  check(new Set(widths).size === 3, 'language positive control: three distinct advances '+widths);
  const element = document.querySelector('span[data-loka-token]');
  const token=+element.dataset.lokaToken, originalService=normal.service(), service=normal.admit();
  normal.replaceService(service);
  const measure=service.ctx.measureText.bind(service.ctx);
  let calls=0, bytes=0;
  service.ctx.measureText=function(text) {
    this.font=this.font.replace('system-ui','"Loka Text Probe"');
    ++calls; bytes+=new TextEncoder().encode(text).length;
    return measure(text);
  };
  const project=(kind,width)=>{
    const height=fixture._fixture_text_probe(kind,width,token);
    element.style.fontFamily='"Loka Text Probe"';
    return height;
  };
  const h=project(0,9);
  check(element.textContent==='A\nV\nx','standalone range kerning at line start');
  const H=parseFloat(element.style.lineHeight);
  check(h===3*H,'three allocated lines');
  check(project(1,100)===2*H && element.textContent==='a\nb','explicit newline = 2H');
  project(2,100);
  check(element.textContent==='漢字😀','CJK and emoji intact');
  const range=document.createRange(); range.selectNodeContents(element);
  const rangeWidth=range.getBoundingClientRect().width;
  const canvasWidth=service.ctx.measureText(element.textContent).width;
  check(Math.abs(rangeWidth-canvasWidth)<1,'Range vs standalone canvas');
  const metrics=service.ctx.measureText('x');
  const half=(H-metrics.fontBoundingBoxAscent-metrics.fontBoundingBoxDescent)/2;
  check(half>=0 && half<1,'conditional half-leading');
  const baselineBox=document.createElement('div');
  service.environment(16).applyToElement(baselineBox);
  Object.assign(baselineBox.style,{fontFamily:'"Loka Text Probe"',lineHeight:H+'px',
    position:'absolute',padding:'0',border:'0',margin:'0'});
  baselineBox.appendChild(document.createTextNode('x'));
  const marker=document.createElement('span');
  marker.style.cssText='display:inline-block;width:0;height:0;padding:0;border:0;vertical-align:baseline';
  baselineBox.appendChild(marker); document.body.appendChild(baselineBox);
  const baseline=marker.getBoundingClientRect().top-baselineBox.getBoundingClientRect().top;
  check(Math.abs(baseline-(metrics.fontBoundingBoxAscent+half))<0.1,'fractional CSS baseline');
  baselineBox.remove();
  check(getComputedStyle(element).overflow==='hidden','clip');
  const costs=[];
  for(const kind of [3,4]) {
    calls=bytes=0; const start=performance.now(); project(kind,80);
    costs.push({kind,calls,bytes,ms:performance.now()-start});
  }
  service.ctx.measureText=measure;
  normal.replaceService(originalService);
  const button=document.querySelector('button[data-loka-token]');
  const original=button.textContent;
  for(const label of [original,'']) {
    button.textContent=label; const css=getComputedStyle(button), rect=button.getBoundingClientRect();
    check(css.appearance==='none' && css.boxSizing==='border-box','Button appearance');
    check(parseFloat(css.paddingLeft)===8 && parseFloat(css.paddingTop)===4,'Button padding');
    check(parseFloat(css.borderLeftWidth)===1 && parseFloat(css.borderTopWidth)===1,'Button border');
    check(rect.height===parseFloat(css.lineHeight)+10,'Button height H+10');
    const width=button.style.width; button.style.width='10px';
    check(button.getBoundingClientRect().width>=18,'narrow Button preserves padding and border minimum');
    button.style.width=width;
  }
  button.textContent=original;
  // A separate mount has no prior snapshot; refuse only its Button label probes.
  const failed = await LokaWebCounter();
  const nativeMeasure = CanvasRenderingContext2D.prototype.measureText;
  CanvasRenderingContext2D.prototype.measureText = function(text) {
    if (text === 'Increment') throw new Error('forced first Button measurement refusal');
    return nativeMeasure.call(this, text);
  };
  try {
    failed._fixture_configure(1,0);
    check(failed.web.start() === 0, 'failure fixture starts');
    const buttons = document.querySelectorAll('button[data-loka-token]');
    const empty = buttons[buttons.length - 1];
    check(empty.style.height === '0px', 'first failure has zero layout height');
    check(getComputedStyle(empty).visibility === 'hidden', 'first failure hides Button chrome');
    const box = empty.getBoundingClientRect();
    check(box.width > 0 && box.height > 0, 'chrome box hit-test positive geometry');
    check(document.elementFromPoint(box.left + box.width / 2, box.top + box.height / 2) !== empty,
      'first failure excludes Button from hit testing');
    CanvasRenderingContext2D.prototype.measureText = nativeMeasure;
    failed._web_wake(2, performance.now());
    check(getComputedStyle(empty).visibility === 'visible', 'recovery projects Button');
    check(empty.textContent === 'Increment', 'recovery publishes label');
  } finally {
    CanvasRenderingContext2D.prototype.measureText = nativeMeasure;
    failed.web.dispose(); await failed.web.completed;
  }
  console.log('text projection costs '+JSON.stringify(costs));
  const before = element.style.cssText;
  check(normal.badDescriptor(+element.dataset.lokaToken) === false, 'release-active protocol refusal');
  check(element.style.cssText === before, 'protocol refusal preserves element');
  return {widths, costs, cells: ['polluted ancestor', 'language positive control', 'regular-only bold synthesis',
    'detached canvas', 'language fixed after first key', 'protocol refusal', 'first Button failure and recovery']};
};
