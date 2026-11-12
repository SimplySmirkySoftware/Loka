import assert from 'node:assert/strict';

/** Cost: driver per skin; visits only each test module's projected leaves. */
export async function runSkinCells(client, navigate) {
  await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  const skins=[['web',12,4,9],['win32',6,6,10],['macos',6,6,16],['classic',8,8,12]];
  for(const [name,gap,horizontal,margin] of skins) {
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
    const lifecycle=await client.evaluate(`(()=>{const first=document.querySelector('button'),cell=document.querySelectorAll('button')[1];first.click();return {oldGone:!cell.isConnected,count:document.querySelectorAll('button').length};})()`);
    assert(lifecycle.oldGone);assert.equal(lifecycle.count,65);
    await client.evaluate('web.dispose();web.completed.then(()=>true)');
    console.log(`SkinMine ${name} gap, click and New Game passed`);
  }
  await navigate('index.html');
  const together=await client.evaluate(`(async()=>{
    const a=await LokaWebCounter({lokaSkin:'web'}),b=await LokaWebCounter({lokaSkin:'classic'});
    a.lokaSkin='macos'; // Selection belongs to the first BOOTSTRAP pull, after factory completion.
    a._fixture_configure(-31,0);b._fixture_configure(-31,0);
    if(a.web.start()!==0 || b.web.start()!==0) throw new Error('two skins start');
    const roots=[...document.querySelectorAll('body > div[data-loka-token]')];
    a.lokaSkin='web';b.lokaSkin='win32';
    for(const [i,m] of [a,b].entries()) m.web.resize(Number(roots[i].dataset.lokaToken),200,120);
    const x=roots.map(r=>parseFloat(r.querySelector('span').style.left));
    a.web.dispose();await a.web.completed;
    b.web.resize(Number(roots[1].dataset.lokaToken),180,120);
    const remaining=parseFloat(roots[1].querySelector('span').style.left);
    b.web.dispose();await b.web.completed;return {x,remaining};
  })()`);
  assert.deepEqual(together,{x:[32,24],remaining:24});
  console.log('SkinModules bootstrap-time choice, simultaneous macos/classic, relayout and independent disposal passed');
}
