import assert from 'node:assert/strict';

/** Real DOM and Linux Chromium keyboard evidence; native picker behavior is OS-specific. */
export async function runPopupCells(client, navigate) {
  await navigate('hello.html');
  assert.equal(await client.evaluate('web.start()'),0);
  const initial=await client.evaluate(`(()=>{
    window.popup=document.querySelector('select');
    const button=[...document.querySelectorAll('button')].find(e=>e.textContent==='Probe Button');
    const r=popup.getBoundingClientRect(),s=getComputedStyle(popup);
    popup.scrollIntoView();popup.focus();
    return {options:[...popup.options].map(e=>e.textContent),height:r.height,
      buttonHeight:button.getBoundingClientRect().height,visible:s.visibility,selected:popup.selectedIndex};
  })()`);
  assert.deepEqual(initial.options,['Apple','Banana','Cherry','Grape']);
  assert(initial.height>0 && initial.height===initial.buttonHeight);
  assert.equal(initial.visible,'visible');assert.equal(initial.selected,0);
  await client.send('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowDown',code:'ArrowDown',windowsVirtualKeyCode:40});
  await client.send('Input.dispatchKeyEvent',{type:'keyUp',key:'ArrowDown',code:'ArrowDown',windowsVirtualKeyCode:40});
  assert.equal(await client.evaluate('popup.selectedIndex'),1);
  assert(await client.evaluate("document.body.textContent.includes('You chose Banana')"));
  await client.evaluate('web.dispose();web.completed.then(()=>true)');

  await navigate('index.html');
  assert.equal(await client.evaluate('fixture._fixture_configure(-400,0);web.start()'),0);
  const stable=await client.evaluate(`(()=>{
    window.popup=document.querySelector('select');
    const options=[...popup.options],texts=options.map(e=>e.firstChild);
    fixture._fixture_popup_command(0);
    return {same:options.every((e,i)=>e===popup.options[i] && texts[i]===e.firstChild),
      literal:popup.options[1].textContent,markup:popup.querySelectorAll('b').length};
  })()`);
  assert(stable.same);assert.equal(stable.literal,'<b>Banana');assert.equal(stable.markup,0);
  await client.evaluate('fixture._fixture_popup_command(1)');
  const refused=await client.evaluate(`(()=>{
    const previous=popup.selectedIndex,state=fixture._fixture_popup_selected();
    let calls=0,atExport=-99,candidate=-99;
    const select=web.select;
    web.select=(token,index)=>{++calls;atExport=popup.selectedIndex;candidate=index;return select(token,index);};
    popup.selectedIndex=1;
    popup.dispatchEvent(new Event('change'));
    web.select=select;
    return {previous,state,after:popup.selectedIndex,afterState:fixture._fixture_popup_selected(),
      calls,atExport,candidate,disabled:popup.disabled};
  })()`);
  assert(refused.disabled);assert.equal(refused.calls,1);assert.equal(refused.candidate,1);
  assert.notEqual(refused.previous,refused.candidate);
  assert.equal(refused.atExport,refused.previous);
  assert.equal(refused.after,refused.previous);assert.equal(refused.afterState,refused.state);
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
  console.log('Popup DOM: HelloWorld geometry/options, Linux ArrowDown, stable literal options, disabled restore-before-export passed');
}
