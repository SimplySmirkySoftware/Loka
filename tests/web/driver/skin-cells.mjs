import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const vocabulary=JSON.parse(readFileSync(new URL('../../../tools/style-vocab.json',import.meta.url),'utf8'));
const rgb=value=>value.startsWith('#') ? 'rgb('+value.slice(1).match(/../g).map(v=>parseInt(v,16)).join(', ')+')' : value;

/** Cost: driver per skin; visits only each test module's projected leaves. */
export async function runSkinCells(client, navigate) {
  await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  const skins=[['web',12,4,9],['win32',6,6,10],['macos',6,6,16],['classic',8,8,12]];
  for(const [name,gap,horizontal,margin] of skins) {
    await paintCells(client,navigate,name);
    await navigate('index.html');
    const fixture=await client.evaluate(`(async()=>{
      const m=await LokaWebCounter({lokaSkin:${JSON.stringify(name)}});
      m._fixture_configure(-30,0);
      if(m.web.start()!==0) throw new Error('skin fixture start');
      const root=[...document.querySelectorAll('body > div[data-loka-token]')].at(-1);
      const sample=()=>[...root.querySelectorAll('span')].map(e=>({x:parseFloat(e.style.left),y:parseFloat(e.style.top),w:parseFloat(e.style.width),h:parseFloat(e.style.height)}));
      const before=sample();m.lokaSkin='other';m.web.resize(Number(root.dataset.lokaToken),210,120);
      const after=sample();m.web.dispose();await m.web.completed;return {before,after};
    })()`);
    for(const rows of [fixture.before,fixture.after]) {
      assert.equal(rows[0].x,margin);assert.equal(rows[0].y,margin);
      assert.equal(rows[1].x-rows[0].x-rows[0].w,horizontal);
      assert.equal(rows[2].y-rows[0].y-rows[0].h,gap);
      assert.equal(rows[3].x-rows[2].x-rows[2].w,3);
    }
    console.log(`SkinFixture ${name} factory option, gaps, override and frozen selection passed`);

    await navigate('hello.html?skin='+name);
    assert.equal(await client.evaluate('web.start()'),0);
    for(const width of [399,400,420]) {
      const measured=await client.evaluate(`(()=>{
        const root=document.querySelector('body > div[data-loka-token]');root.style.width='${width}px';
        const css=getComputedStyle(root);
        return parseFloat(css.width)-(css.boxSizing==='border-box'?parseFloat(css.paddingLeft)+parseFloat(css.paddingRight)+parseFloat(css.borderLeftWidth)+parseFloat(css.borderRightWidth):0);
      })()`);
      assert.equal(measured,width,'root content-box width before layout assertion');
      const rows=await client.evaluate(`(()=>{
        const root=document.querySelector('body > div[data-loka-token]');web.resize(Number(root.dataset.lokaToken),${width},330);
        const leaves=[...root.querySelectorAll('span,button')];
        const rect=text=>{const e=leaves.find(e=>e.textContent.includes(text));return {x:parseFloat(e.style.left),y:parseFloat(e.style.top)}};
        return {left:rect('Loka Sample'),right:rect('BMI Calculator')};
      })()`);
      assert.equal(rows.left.x,margin);assert.equal(rows.left.y,margin);
      if(width===399) {assert.equal(rows.right.x,rows.left.x);assert(rows.right.y>rows.left.y);}
      else assert(rows.right.x>rows.left.x);
    }
    await client.evaluate('web.dispose();web.completed.then(()=>true)');
    console.log(`SkinHello ${name} root content-box 399/400/420, padding and breakpoint passed`);

    await navigate('mine.html?skin='+name);
    assert.equal(await client.evaluate('web.start()'),0);
    // Named normal fixture: 220x240; cramped overflow is tested separately.
    const mine=await client.evaluate(`(()=>{
      const root=document.querySelector('body > div[data-loka-token]');
      const buttons=[...root.querySelectorAll('button')], first=buttons[0], cell=buttons[1];
      const box=e=>({x:parseFloat(e.style.left),y:parseFloat(e.style.top),w:parseFloat(e.style.width),h:parseFloat(e.style.height)});
      const a=box(first),b=box(cell),r=root.getBoundingClientRect(),c=cell.getBoundingClientRect();
      const x=c.x+c.width/2,y=c.y+c.height/2;
      return {a,b,gap:b.y-a.y-a.h,x,y,hit:document.elementFromPoint(x,y)===cell,
        contained:buttons.every(e=>{const q=e.getBoundingClientRect();return q.left>=r.left&&q.top>=r.top&&q.right<=r.right&&q.bottom<=r.bottom}),
        documentFits:document.documentElement.scrollWidth<=document.documentElement.clientWidth&&document.documentElement.scrollHeight<=document.documentElement.clientHeight,
        overflow:[root.scrollWidth,root.scrollHeight],size:[root.clientWidth,root.clientHeight]};
    })()`);
    assert.deepEqual([mine.a.x,mine.a.y],[margin,margin]);
    assert.equal(mine.a.w,220-2*margin);assert.equal(mine.gap,gap);
    assert.equal(mine.b.w,Math.floor((220-2*margin)/8));
    assert.equal(mine.b.h,Math.floor((240-2*margin-mine.a.h-gap)/8));
    assert(mine.contained && mine.hit && mine.documentFits);assert.deepEqual(mine.overflow,mine.size);
    for(const type of ['mousePressed','mouseReleased'])
      await client.send('Input.dispatchMouseEvent',{type,x:mine.x,y:mine.y,button:'left',clickCount:1});
    const clicked=await client.evaluate(`(()=>{
      const buttons=[...document.querySelectorAll('button')],first=buttons[0],cell=buttons[1];
      const text=cell.textContent,hitFocused=document.activeElement===cell;first.focus();return {text,hitFocused,focused:document.activeElement===first};
    })()`);
    assert.notEqual(clicked.text,'.');assert(clicked.hitFocused && clicked.focused);
    const lifecycle=await client.evaluate(`(()=>{const first=document.querySelector('button'),cell=document.querySelectorAll('button')[1];const paint=cell.style.background;fixture.lokaSkin='other';first.click();const fresh=document.querySelectorAll('button')[1];return {oldGone:!cell.isConnected,count:document.querySelectorAll('button').length,paintKept:fresh.style.background===paint && fresh.style.borderRadius==='0px'};})()`);
    assert(lifecycle.oldGone && lifecycle.paintKept);assert.equal(lifecycle.count,65);
    await client.evaluate('web.dispose();web.completed.then(()=>true)');
    console.log(`SkinMine ${name} gap, click and New Game passed`);
  }
  await navigate('index.html');
  const together=await client.evaluate(`(async()=>{
    const sheets=()=>({adopted:document.adoptedStyleSheets.length,styles:document.querySelectorAll('style').length});
    const originalSheets=[...document.adoptedStyleSheets];
    const before=sheets();
    const a=await LokaWebCounter({lokaSkin:'web'}),b=await LokaWebCounter({lokaSkin:'classic'});
    a.lokaSkin='macos'; // Selection belongs to the first BOOTSTRAP pull, after factory completion.
    a._fixture_configure(-31,0);b._fixture_configure(-31,0);
    if(a.web.start()!==0 || b.web.start()!==0) throw new Error('two skins start');
    const roots=[...document.querySelectorAll('body > div[data-loka-token]')];
    a.lokaSkin='web';b.lokaSkin='win32';
    for(const [i,m] of [a,b].entries()) m.web.resize(Number(roots[i].dataset.lokaToken),200,120);
    const colours=roots.map(r=>getComputedStyle(r).backgroundColor);
    const textColours=roots.map(r=>getComputedStyle(r.querySelector('span')).color);
    const during=sheets();
    const x=roots.map(r=>parseFloat(r.querySelector('span').style.left));
    a.web.dispose();await a.web.completed;
    b.web.resize(Number(roots[1].dataset.lokaToken),180,120);
    const remaining=parseFloat(roots[1].querySelector('span').style.left);
    const remainingColour=getComputedStyle(roots[1]).backgroundColor;
    b.web.dispose();await b.web.completed;return {x,remaining,colours,textColours,remainingColour,before,during,after:sheets(),sameSheets:originalSheets.every((sheet,i)=>document.adoptedStyleSheets[i]===sheet)};
  })()`);
  assert.deepEqual(together.x,[32,24]);assert.equal(together.remaining,24);
  assert.deepEqual(together.colours,['rgb(236, 236, 236)','rgb(221, 221, 221)']);
  assert.equal(together.remainingColour,together.colours[1]);
  assert.deepEqual(together.textColours,['rgb(38, 38, 38)','rgb(0, 0, 0)']);
  assert.deepEqual(together.before,together.during);assert.deepEqual(together.before,together.after);assert(together.sameSheets);
  console.log('SkinModules bootstrap-time choice, simultaneous macos/classic, relayout and independent disposal passed');
}

/** Cost: driver per skin/theme; reads only fixture-owned DOM and two temporary colour probes. */
async function paintCells(client,navigate,name) {
  const look=vocabulary.webSkins.find(row=>row.name===name).look;
  await navigate('index.html');
  await client.evaluate(`(async()=>{
    // Exactly the preview host theme rules; class changes remain live after startup.
    const rules=document.createElement('style');
    rules.textContent='body.vscode-light,body.vscode-high-contrast-light{color-scheme:light}body.vscode-dark,body.vscode-high-contrast{color-scheme:dark}';
    document.head.appendChild(rules);
    window.paintModule=await LokaWebCounter({lokaSkin:${JSON.stringify(name)}});
    paintModule._fixture_configure(-208,0);
    if(paintModule.web.start()!==0) throw Error('paint fixture start');
    window.paintRoot=[...document.querySelectorAll('body > div[data-loka-token]')].at(-1);
    paintModule.lokaSkin='other';
  })()`);
  const sample=async()=>client.evaluate(`(()=>{
    const read=e=>{const s=getComputedStyle(e);return {background:s.backgroundColor,border:s.borderTopColor,
      radius:s.borderRadius,align:s.textAlign,color:s.color,scheme:s.colorScheme,
      padding:[s.paddingLeft,s.paddingRight,s.paddingTop,s.paddingBottom],borderWidth:s.borderTopWidth,
      width:parseFloat(s.width),content:e.clientWidth-parseFloat(s.paddingLeft)-parseFloat(s.paddingRight),
      inline:{background:e.style.background,border:e.style.borderColor,radius:e.style.borderRadius,
        text:e.style.getPropertyValue('--loka-text'),secondary:e.style.getPropertyValue('--loka-secondary'),color:e.style.color}}};
    const buttons=[...paintRoot.querySelectorAll('button')],texts=[...paintRoot.querySelectorAll('span')];
    const probe=document.createElement('div');paintRoot.appendChild(probe);
    const system={};for(const color of ['Canvas','CanvasText','GrayText','ButtonFace','ButtonBorder']) {
      probe.style.color=color;system[color]=getComputedStyle(probe).color;
    }probe.remove();
    return {root:read(paintRoot),button:read(buttons[0]),cell:read(buttons[1]),texts:texts.map(read),system};
  })()`);
  for(const theme of ['light','dark']) {
    await client.evaluate(`document.body.className='vscode-${theme}';paintModule._fixture_cell_command(4)`);
    let values=await sample();
    const color=value=>values.system[value]??rgb(value);
    assert.equal(values.root.background,color(look.ground));
    assert.equal(values.root.inline.text,look.text);assert.equal(values.root.inline.secondary,look.secondary);
    assert.equal(values.root.scheme,look.scheme==='auto'?theme:'light');
    for(const [kind,face,border,radius] of [['button',look.face,look.border,look.radius],['cell',look.cellFace,look.cellBorder,0]]) {
      const v=values[kind];assert.equal(v.background,color(face));assert.equal(v.border,color(border));
      assert.equal(v.radius,radius+'px');assert.equal(v.inline.radius,radius+'px');
      assert(v.inline.background && v.inline.border);assert.equal(v.align,'center');
      assert.equal(v.color,color(look.text));assert.equal(v.inline.color,'var(--loka-text)');
      assert.deepEqual(v.padding,['8px','8px','4px','4px']);assert.equal(v.borderWidth,'1px');
      assert.equal(v.content,v.width-18);
    }
    assert.equal(values.texts[0].color,color(look.text));assert.equal(values.texts[1].color,color(look.secondary));
    await client.evaluate('paintModule._fixture_cell_command(3);paintModule._fixture_cell_command(1)');
    values=await sample();assert.equal(values.button.color,color(look.disabledText));
    assert.equal(values.button.inline.text,look.disabledText);
    await client.evaluate('paintModule._fixture_cell_command(5);paintModule._fixture_cell_command(6)');
    values=await sample();assert.equal(values.button.color,color(look.disabledText));assert.equal(values.button.inline.text,look.disabledText);
    await client.evaluate('paintModule._fixture_cell_command(4)');
    values=await sample();assert.equal(values.button.color,color(look.text));assert.equal(values.button.inline.text,'');
    assert.equal(values.cell.background,color(look.cellFace));assert.equal(values.cell.radius,'0px');
  }
  // DOM keyboard behavior plus the logical rail's native-focus query (CELL leaf routing).
  await client.evaluate('paintRoot.querySelector("button").focus()');
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  assert.equal(await client.evaluate('document.activeElement===paintRoot.querySelectorAll("button")[1]'),true);
  assert.equal(await client.evaluate('paintModule._fixture_leaf_focused(Number(document.activeElement.dataset.lokaToken))'),1);
  for(const key of [{key:' ',code:'Space',windowsVirtualKeyCode:32},{key:'Enter',code:'Enter',windowsVirtualKeyCode:13,text:'\r'}]) {
    const before=await client.evaluate('paintModule._fixture_cell_hits(0)');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',...key});
    await client.send('Input.dispatchKeyEvent',{type:'keyUp',...key});
    assert.equal(await client.evaluate('paintModule._fixture_cell_hits(0)'),before+1);
    await client.evaluate('paintRoot.querySelector("button").focus()');
    const buttonBefore=await client.evaluate('paintModule._fixture_cell_hits(-1)');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',...key});
    await client.send('Input.dispatchKeyEvent',{type:'keyUp',...key});
    assert.equal(await client.evaluate('paintModule._fixture_cell_hits(-1)'),buttonBefore+1);
    await client.evaluate('paintRoot.querySelectorAll("button")[1].focus()');
  }
  await client.evaluate('paintModule.web.dispose();paintModule.web.completed.then(()=>true)');
  // Dark host probe lives under body, actual scrollbar under the light skin root.
  await client.evaluate(`(async()=>{
    window.paintModule=await LokaWebCounter({lokaSkin:${JSON.stringify(name)}});
    paintModule._fixture_configure(-205,0);if(paintModule.web.start()!==0)throw Error('gutter fixture start');
  })()`);
  const gutter=await client.evaluate(`(()=>{
    const root=[...document.querySelectorAll('body > div[data-loka-token]')].at(-1);
    const scroll=[...root.querySelectorAll('div')].find(e=>getComputedStyle(e).overflowY==='scroll');
    const probe=document.createElement('div');probe.style.cssText='width:100px;height:100px;overflow:scroll;margin:0;padding:0;border:0';
    document.body.appendChild(probe);const expected=probe.offsetWidth-probe.clientWidth;probe.remove();
    return {expected,actual:scroll.offsetWidth-scroll.clientWidth};
  })()`);
  assert.equal(gutter.actual,gutter.expected);
  await client.evaluate('paintModule.web.dispose();paintModule.web.completed.then(()=>true)');
  console.log(`SkinPaint ${name} light/dark host, inline declarations, Button enabled, Cell retention, keyboard and gutter passed`);
}
