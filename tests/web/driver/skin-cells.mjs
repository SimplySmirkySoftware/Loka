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
    const mine=await client.evaluate(`(()=>{
      const buttons=[...document.querySelectorAll('button')], first=buttons[0], cell=buttons[1];
      const gap=parseFloat(cell.style.top)-parseFloat(first.style.top)-parseFloat(first.style.height);
      cell.click();const clicked=cell.textContent;first.click();
      return {gap,clicked,oldGone:!cell.isConnected,count:document.querySelectorAll('button').length};
    })()`);
    assert.equal(mine.gap,gap);assert.notEqual(mine.clicked,'.');assert(mine.oldGone);assert.equal(mine.count,65);
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
  assert.deepEqual(together,{x:[16,12],remaining:12});
  console.log('SkinModules bootstrap-time choice, simultaneous macos/classic, relayout and independent disposal passed');
}
