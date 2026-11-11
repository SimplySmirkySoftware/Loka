import assert from 'node:assert/strict';

/** Cost: driver once per fixture; only its DOM leaves and fixture exports. */
export async function runStackCells(client, navigate) {
  const start = async cell => {
    await navigate('index.html');
    await client.evaluate(`(async()=>{
      document.documentElement.lang='en';
      const face=new FontFace('Loka Text Probe','url(stack-probe.ttf)');
      document.fonts.add(await face.load());
      const measure=CanvasRenderingContext2D.prototype.measureText;
      CanvasRenderingContext2D.prototype.measureText=function(text) {
        this.font=this.font.replace('system-ui','"Loka Text Probe"');
        return measure.call(this,text);
      };
      const style=document.createElement('style');
      style.textContent='[data-loka-token] {font-family:"Loka Text Probe"!important}';
      document.head.appendChild(style);
      fixture._fixture_configure(${-10-cell},0);
      if(web.start()!==0) throw new Error('layout start');
      window.root=document.querySelector('body > div[data-loka-token]');
      window.rootToken=Number(root.dataset.lokaToken);
      window.labels=()=>[...root.querySelectorAll('span,button')].map(e=>({text:e.textContent,
        x:parseFloat(e.style.left),y:parseFloat(e.style.top),w:parseFloat(e.style.width),h:parseFloat(e.style.height)}));
    })()`);
  };
  const stop = () => client.evaluate('web.dispose();web.completed.then(()=>true)');
  await start(3);
  let rows=await client.evaluate('labels()');
  assert.equal(rows[0].y,5); assert.equal(rows[2].y,rows[1].h+14);
  console.log('AlignedTextButton browser passed: '+JSON.stringify(rows));
  const degraded=await client.evaluate(`(()=>{
    const normal=fixture.textTest.service();window.estimateCalls=0;
    window.savedMetricsService=normal;
    fixture.textTest.replaceService(Object.create(normal,{metrics:{value:()=>{++estimateCalls;return null}}}));
    fixture._fixture_layout_command(3,0);
    return {rows:labels(),calls:estimateCalls,pending:fixture.textTest.immediatePending()};
  })()`);
  assert(degraded.calls>0 && degraded.pending,'persistent estimates request another batch');
  assert.equal(degraded.rows[0].y,0);
  assert.equal(degraded.rows[1].y,0);
  assert.equal(degraded.rows[2].y,rows[1].h+4);
  assert.equal(degraded.rows[3].y,rows[1].h+4);
  await client.evaluate('fixture.textTest.replaceService(savedMetricsService);fixture._web_wake(2,performance.now())');
  assert.deepEqual(await client.evaluate('labels()'),rows);
  assert.equal(await client.evaluate('fixture.textTest.immediatePending()'),false);
  console.log('AlignedRowEstimateDegradeAndRecovery browser passed: '+JSON.stringify(degraded));
  await stop();
  await start(4);
  rows=await client.evaluate('labels()');
  await client.evaluate("root.style.width='120px';web.resize(rootToken,120,120)");
  const narrow=await client.evaluate('labels()');
  assert(narrow[0].h>rows[0].h);
  assert.equal(narrow[0].y,5);
  assert.equal(narrow[2].y,Math.max(narrow[0].y+narrow[0].h,narrow[1].y+narrow[1].h)+4);
  console.log('WrappedRowResize browser passed: '+JSON.stringify({rows,narrow}));
  await stop();
  await start(6);
  rows=await client.evaluate('labels()');
  assert(rows[1].x>0 && rows[1].y===0);
  await client.evaluate('fixture._fixture_layout_command(0,1)');
  const column=await client.evaluate('labels()');
  assert(column[1].x===0 && column[1].y>0);
  await client.evaluate('fixture._fixture_layout_command(0,0)');
  assert.deepEqual(await client.evaluate('labels()'),rows);
  console.log('AxisRoundTrip browser passed');
  await stop();
  await start(7);
  for(const h of [35,34,33]) {
    await client.evaluate(`root.style.height='${h}px';web.resize(rootToken,220,${h})`);
    assert.equal(await client.evaluate('fixture._fixture_layout_fact(3)'),Math.max(0,h-34));
  }
  console.log('ColumnRemainingHeight browser passed');
  await stop();
  await start(8);
  await client.evaluate('fixture._fixture_layout_command(1,0);fixture._fixture_layout_command(2,20);fixture._fixture_layout_command(1,1)');
  const origin=await client.evaluate('fixture._fixture_layout_fact(0)');
  assert(origin>0);
  await client.evaluate("root.style.width='400px';web.resize(rootToken,400,120)");
  assert.equal(await client.evaluate('fixture._fixture_layout_fact(0)'),0);
  console.log('RowScrollReveal browser passed');
  await stop();
  await start(9);
  const extent=await client.evaluate(`(()=>{
    root.style.height='20px';web.resize(rootToken,220,20);
    fixture._fixture_layout_command(4,19);
    const scroll=root.firstElementChild;
    window.scrollExtent=()=>parseFloat(scroll.firstElementChild.style.height);
    const before={extent:scrollExtent(),origin:fixture._fixture_layout_fact(1)};
    window.savedMetricsService=fixture.textTest.service();
    fixture.textTest.replaceService(Object.create(savedMetricsService,{metrics:{value:()=>null}}));
    fixture._fixture_layout_command(3,0);
    return {before,after:{extent:scrollExtent(),origin:fixture._fixture_layout_fact(1)}};
  })()`);
  assert(extent.before.extent>extent.after.extent && extent.before.origin>0);
  assert.equal(extent.after.origin,0);
  await client.evaluate('fixture.textTest.replaceService(savedMetricsService);fixture._web_wake(2,performance.now())');
  assert.equal(await client.evaluate('scrollExtent()'),extent.before.extent);
  assert.equal(await client.evaluate('fixture._fixture_layout_fact(1)'),0);
  console.log('AlignedRowEstimateDegradeAndRecovery ScrollView browser passed: '+JSON.stringify(extent));
  await stop();
  // Admission succeeds before a metrics miss; recover without changing props.
  await navigate('index.html');
  const initial=await client.evaluate(`(()=>{
    const original=CanvasRenderingContext2D.prototype.measureText;let calls=0;
    window.restoreMeasure=()=>{CanvasRenderingContext2D.prototype.measureText=original};
    CanvasRenderingContext2D.prototype.measureText=function(text){
      const m=original.call(this,text);
      return ++calls===1?m:{width:m.width,fontBoundingBoxAscent:NaN,fontBoundingBoxDescent:NaN};
    };
    fixture._fixture_configure(-13,0);return web.start();
  })()`);
  assert.equal(initial,0);
  await client.evaluate('restoreMeasure();fixture._web_wake(2,performance.now())');
  assert.equal(await client.evaluate('document.querySelectorAll("button").length'),2);
  console.log('AlignedRowEstimate initial refusal browser passed');
  await stop();
}

/** Cost: driver once; resize the same owned root and inspect its own leaves. */
export async function runHelloRoundTrip(client,navigate) {
  await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  await navigate('hello.html');
  assert.equal(await client.evaluate('web.start()'),0);
  const root=await client.evaluate('Number(document.querySelector("body > div[data-loka-token]").dataset.lokaToken)');
  const records=[];
  for(const width of [420,399,420]) {
    records.push(await client.evaluate(`(()=>{
      const root=document.querySelector('body > div[data-loka-token]');root.style.width='${width}px';
      web.resize(${root},${width},330);
      const leaves=[...root.querySelectorAll('span,button')];
      const rect=text=>leaves.find(e=>e.textContent.includes(text)).getBoundingClientRect();
      const a=rect('Loka Sample'),b=rect('BMI Calculator');
      return {token:Number(root.dataset.lokaToken),width:root.clientWidth,left:[a.x,a.y],right:[b.x,b.y]};
    })()`));
  }
  for(const r of records) assert.equal(r.token,root);
  assert.deepEqual(records.map(r=>r.width),[420,399,420]);
  assert(records[0].right[0]>records[0].left[0]);
  assert.equal(records[1].right[0],records[1].left[0]);
  assert(records[1].right[1]>records[1].left[1]);
  assert.deepEqual(records[0].left,records[2].left);
  assert.deepEqual(records[0].right,records[2].right);
  const clip=await client.evaluate(`(()=>{
    const add=[...document.querySelectorAll('button')].find(e=>e.textContent==='Add +');
    const message=[...document.querySelectorAll('span')].find(e=>e.textContent.startsWith('Hello, Loka!'));
    const before=message.textContent;
    for(let i=0;i<20;++i)add.click();
    return {before,text:message.textContent,width:message.clientWidth,scrollWidth:message.scrollWidth,
      overflow:getComputedStyle(message).overflow};
  })()`);
  assert.equal(clip.text,clip.before+' +Loka'.repeat(20));
  assert(clip.scrollWidth>clip.width && clip.overflow==='hidden','growing message clips in shared seat');
  console.log('HelloWeb same-root 420 -> 399 -> 420 passed: '+JSON.stringify(records));
  console.log('HelloWeb clipped message record: '+JSON.stringify(clip));
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
}
