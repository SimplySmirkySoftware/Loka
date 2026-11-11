import assert from 'node:assert/strict';

/** Cost: driver once; inspects only the fixture's four retained DOM Buttons. */
export async function runGridCells(client, navigate) {
  await navigate('index.html');
  await client.evaluate(`(()=>{
    fixture._fixture_configure(-100,0);
    if(web.start()!==0) throw new Error('grid start');
    window.gridButtons=[...document.querySelectorAll('button')];
    window.gridRects=()=>gridButtons.map(e=>{
      const r=e.getBoundingClientRect(),p=e.parentElement.getBoundingClientRect();
      return {x:r.x-p.x,y:r.y-p.y,w:r.width,h:r.height,token:e.dataset.lokaToken};
    });
  })()`);
  const before = await client.evaluate('gridRects()');
  assert.equal(before.length,4);
  assert.deepEqual(before.map(r=>[r.x,r.y,r.w]),[[0,0,50],[50,0,50],[0,40,50],[50,40,50]]);
  assert(before.every(r=>r.h>0 && r.h<40),'Button uses measured height, not cell height');
  console.log('Grid2x2 browser rects passed: '+JSON.stringify(before));
  const shrink = await client.evaluate(`(()=>{
    fixture._fixture_grid_capacity(1,1);
    const beforeClicks=fixture._fixture_grid_clicks();
    gridButtons[3].click();
    return {rects:gridRects(),same:gridButtons.every(e=>e.isConnected && document.querySelector('[data-loka-token="'+e.dataset.lokaToken+'"]')===e),
      visible:gridButtons.every(e=>getComputedStyle(e).display!=='none' && getComputedStyle(e).visibility!=='hidden'),
      clicks:fixture._fixture_grid_clicks()-beforeClicks};
  })()`);
  assert(shrink.same && shrink.visible);
  assert.equal(shrink.clicks,1,'over-capacity attached Button keeps input route');
  assert.equal(shrink.rects[0].w,100);
  assert.deepEqual(shrink.rects.slice(1),before.slice(1),'undispatched DOM keeps previous rects');
  await client.evaluate('fixture._fixture_grid_capacity(4,1)');
  const restored = await client.evaluate('gridRects()');
  assert.deepEqual(restored.map(r=>[r.x,r.y,r.w]),[[0,0,100],[0,20,100],[0,40,100],[0,60,100]]);
  assert.deepEqual(restored.map(r=>r.token),before.map(r=>r.token));
  console.log('GridCapacityHistory browser retained DOM/rect/input and restore passed');
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
}
