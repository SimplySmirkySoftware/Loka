import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';

/** Driver-only DOM evidence. Each hit starts from a fresh fixture generation. */
export async function runCellCells(client, navigate) {
  const stop = async () => {
    await client.evaluate('web.dispose();web.completed.then(()=>true)');
    assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
  };
  const inspect = async () => client.evaluate(`(()=>{
    const buttons=[...document.querySelectorAll('button')];
    window.newGame=buttons.find(e=>e.textContent==='New Game');
    window.cells=buttons.filter(e=>e!==newGame);
    window.root=newGame.parentElement;
    window.rootToken=Number(root.dataset.lokaToken);
    return {root:{w:root.clientWidth,h:root.clientHeight},rows:cells.map(e=>{
      const r=e.getBoundingClientRect(),s=getComputedStyle(e);
      return {x:r.x,y:r.y,w:r.width,h:r.height,styleW:e.style.width,styleH:e.style.height,
        align:s.textAlign,text:e.textContent,token:e.dataset.lokaToken};
    }),buttonAlign:getComputedStyle(newGame).textAlign};
  })()`);
  const hit = async index => {
    const point=await client.evaluate(`(()=>{
      const e=cells[${index}],r=e.getBoundingClientRect(),x=r.x+r.width/2,y=r.y+r.height/2;
      return {x,y,hit:document.elementFromPoint(x,y)===e};
    })()`);
    assert(point.hit,`Cell ${index} measured centre hit target`);
    await client.send('Input.dispatchMouseEvent',{type:'mousePressed',x:point.x,y:point.y,button:'left',clickCount:1});
    await client.send('Input.dispatchMouseEvent',{type:'mouseReleased',x:point.x,y:point.y,button:'left',clickCount:1});
  };
  await navigate('index.html');
  await client.evaluate(`(()=>{
    window.events=[];
    const removeListener=EventTarget.prototype.removeEventListener;
    EventTarget.prototype.removeEventListener=function(type,...args){
      if(type==='click' && this.dataset?.lokaToken) events.push(['listener',this.dataset.lokaToken]);
      return removeListener.call(this,type,...args);
    };
    const remove=Element.prototype.remove;
    Element.prototype.remove=function(){
      if(this.dataset?.lokaToken) events.push(['release',this.dataset.lokaToken]);
      return remove.call(this);
    };
    fixture._fixture_configure(-201,0); if(web.start()!==0)throw Error('cells start');
  })()`);
  let geometry=await inspect();
  assert.equal(geometry.rows.length,64);
  assert.equal(new Set(geometry.rows.map(r=>r.x)).size,8);
  assert.equal(new Set(geometry.rows.map(r=>r.y)).size,8);
  assert(geometry.root.w>0 && geometry.root.h>0);
  assert(geometry.rows.every(r=>r.w>0 && r.h>0 && r.align==='center'));
  assert.equal(geometry.buttonAlign,'left');
  for(let index=0;index<64;++index) {
    await client.evaluate('fixture._fixture_cell_command(0)');
    await inspect();
    await hit(index);
    const counts=await client.evaluate('Array.from({length:64},(_,i)=>fixture._fixture_cell_hits(i))');
    assert.deepEqual(counts,Array.from({length:64},(_,i)=>i<=index?1:0));
  }
  console.log('CellAllCentres 64 reset generations CDP hit identities passed');
  await client.evaluate('newGame.focus()');
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9});
  assert.equal(await client.evaluate('document.activeElement===cells[0]'),true);
  for(const key of [{key:' ',code:'Space',windowsVirtualKeyCode:32},{key:'Enter',code:'Enter',windowsVirtualKeyCode:13,text:'\r'}]) {
    const before=await client.evaluate('fixture._fixture_cell_hits(0)');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',...key});
    await client.send('Input.dispatchKeyEvent',{type:'keyUp',...key});
    assert.equal(await client.evaluate('fixture._fixture_cell_hits(0)'),before+1);
  }
  console.log('CellKeyboard focus Space and Enter passed');

  const lifecycle=await client.evaluate(`(()=>{
    window.oldCells=cells.slice();const oldTokens=oldCells.map(e=>e.dataset.lokaToken),kept=newGame;
    const before=Array.from({length:64},(_,i)=>fixture._fixture_cell_hits(i));
    newGame.click();
    oldCells.forEach(e=>e.click());
    const afterDOM=Array.from({length:64},(_,i)=>fixture._fixture_cell_hits(i));
    oldTokens.forEach(t=>web.click(Number(t)));
    const afterToken=Array.from({length:64},(_,i)=>fixture._fixture_cell_hits(i));
    return {before,afterDOM,afterToken,kept:kept.isConnected,
      removed:oldCells.every(e=>!e.isConnected),events:oldTokens.map(t=>events.filter(e=>e[1]===t).map(e=>e[0]))};
  })()`);
  assert.deepEqual(lifecycle.afterDOM,lifecycle.before);
  assert.deepEqual(lifecycle.afterToken,lifecycle.before);
  assert(lifecycle.kept && lifecycle.removed);
  assert(lifecycle.events.every(e=>JSON.stringify(e)===JSON.stringify(['listener','release'])));
  assert((await inspect()).rows.every(r=>r.text==='.' && r.align==='center'));
  console.log('CellLifecycle old DOM, old token, listener-before-release once, retained New Game passed');
  await client.evaluate("root.style.width='40px';web.resize(rootToken,40,240)");
  const narrow=await inspect();
  assert(narrow.rows.some(r=>r.w>parseFloat(r.styleW)),'CSS chrome enlarges narrow cells');
  await client.evaluate("root.style.height='3px';web.resize(rootToken,40,3)");
  const cramped=await inspect();
  assert(cramped.rows.every(r=>r.styleH==='20px'));
  assert.equal(new Set(cramped.rows.map(r=>r.y)).size,1);
  console.log('CellCramped DOM overlap accepted: '+JSON.stringify({narrow,cramped}));
  await stop();

  await navigate('index.html');
  await client.evaluate('fixture._fixture_configure(-200,0);web.start()');
  assert.deepEqual(await client.evaluate("[...document.querySelectorAll('button')].map(e=>[e.textContent,getComputedStyle(e).textAlign])"),
    [['button','left'],['a b','center'],['owned','center'],['null','center']]);
  await client.evaluate(`window.savedService=fixture.textTest.service();
    fixture.textTest.replaceService(Object.create(savedService,{metrics:{value:()=>null}}));
    fixture._fixture_cell_command(2)`);
  assert.equal(await client.evaluate("document.querySelectorAll('button')[1].textContent"),'a b');
  await client.evaluate('fixture.textTest.replaceService(savedService);fixture._fixture_cell_command(1)');
  assert.deepEqual(await client.evaluate("[...document.querySelectorAll('button')].slice(0,2).map(e=>[e.textContent,getComputedStyle(e).textAlign])"),
    [['button','left'],['changed','center']]);
  console.log('CellAlignment normalized CENTER / Button LEFT render and refusal recovery passed');
  await stop();

  // Seed 1 board is independently calculated with the production algorithm's fixed 32-bit arithmetic.
  let seed=1; const mines=new Set();
  while(mines.size<10){seed=(Math.imul(seed,1664525)+1013904223)>>>0;mines.add((seed>>>16)%64);}
  const adjacent=index=>{let n=0;for(let y=-1;y<=1;++y)for(let x=-1;x<=1;++x){
    const r=(index>>3)+y,c=(index%8)+x;if((x||y)&&r>=0&&r<8&&c>=0&&c<8&&mines.has(r*8+c))++n;
  }return n;};
  for(const kind of ['blank','number','mine']) {
    await navigate('index.html');
    await client.evaluate('fixture._fixture_configure(-301,0);web.start()');
    await inspect();
    const index=Array.from({length:64},(_,i)=>i).find(i=>kind==='mine'?mines.has(i):!mines.has(i)&&(kind==='blank'?adjacent(i)===0:adjacent(i)>0));
    await hit(index);
    const value=await client.evaluate(`cells[${index}].textContent`);
    assert.equal(value,kind==='mine'?'X':kind==='blank'?' ':String(adjacent(index)));
    const observation=await client.evaluate(`(()=>{const e=cells[${index}],r=document.createRange();r.selectNodeContents(e);
      const g=r.getBoundingClientRect(),b=e.getBoundingClientRect();return {glyph:{x:g.x,y:g.y,w:g.width,h:g.height},box:{x:b.x,y:b.y,w:b.width,h:b.height},text:e.textContent};})()`);
    const shot=await client.send('Page.captureScreenshot',{format:'png'});
    await writeFile(new URL(`../../../build/cell-${kind}.png`,import.meta.url),Buffer.from(shot.data,'base64'));
    console.log('CellSeeded '+kind+' CDP outcome and glyph observation: '+JSON.stringify(observation));
    await stop();
  }
  await navigate('mine.html');
  assert.equal(await client.evaluate('web.start()'),0);
  geometry=await inspect();
  assert.equal(geometry.rows.length,64);
  assert.equal(new Set(geometry.rows.map(r=>r.x)).size,8);
  assert.equal(new Set(geometry.rows.map(r=>r.y)).size,8);
  assert.deepEqual(geometry.root,{w:220,h:240});
  assert(geometry.rows.every(r=>r.w>0 && r.h>0));
  await client.evaluate('window.kept=newGame;window.old=cells.slice();newGame.click()');
  assert((await inspect()).rows.every(r=>r.text==='.' && r.h>0));
  assert.equal(await client.evaluate('newGame===kept && old.every(e=>!e.isConnected)'),true);
  await stop();
  console.log('MineProduction real module 220x240, 65 Buttons, 8x8, New Game, disposal passed');
}
