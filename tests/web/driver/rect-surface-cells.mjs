import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';

/** Cost: driver once; three placements of its own fixture and one diagnostic capture. */
export async function runRectSurfaceCells(client, navigate) {
  await navigate('index.html');
  assert.equal(await client.evaluate(`(()=>{
    window.surfaceListeners=[];
    const add=EventTarget.prototype.addEventListener;
    window.restoreSurfaceListenerProbe=()=>EventTarget.prototype.addEventListener=add;
    EventTarget.prototype.addEventListener=function(type,...args){
      if(this.dataset?.lokaToken) surfaceListeners.push([Number(this.dataset.lokaToken),type]);
      return add.call(this,type,...args);
    };
    fixture._fixture_configure(-500,0);
    return web.start();
  })()`),0);
  const token=await client.evaluate('fixture._fixture_surface_token()');
  assert(token>0);
  for(const [width,height] of [[1,1],[3,3],[90,45]]) {
    const result=await client.evaluate(`(()=>{
      fixture._fixture_surface_size(${width},${height});
      const e=document.querySelector('[data-loka-token="${token}"]');
      const r=e.getBoundingClientRect(),s=getComputedStyle(e);
      return {width:r.width,height:r.height,text:e.textContent,background:s.backgroundImage,
        outline:s.outlineStyle,outlineWidth:s.outlineWidth,offset:s.outlineOffset,
        border:s.borderTopWidth,padding:s.paddingTop,align:s.textAlign,
        pointer:s.pointerEvents,select:s.userSelect,visible:s.visibility,
        listeners:surfaceListeners.filter(row=>row[0]===${token})};
    })()`);
    assert.equal(result.width,width);assert.equal(result.height,height);
    assert.equal(result.text,'RectSurface');assert.match(result.background,/gradient/);
    assert.equal(result.outline,'dashed');assert.equal(result.outlineWidth,'1px');
    assert.equal(result.offset,'-1px');assert.equal(result.border,'0px');assert.equal(result.padding,'0px');
    assert.equal(result.align,'left');assert.equal(result.pointer,'none');assert.equal(result.select,'none');
    assert.equal(result.visible,'visible');assert.deepEqual(result.listeners,[]);
  }
  // capture.mjs consumes only the shipping example catalog; this fixture capture belongs here.
  const capture=await client.send('Page.captureScreenshot',{format:'png'});
  await writeFile('build/web-rect-surface.png',Buffer.from(capture.data,'base64'));
  await client.evaluate('restoreSurfaceListenerProbe();web.dispose();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
  console.log('RectSurface DOM bounds, fixed label, hatch, outline and no listener passed');
}
