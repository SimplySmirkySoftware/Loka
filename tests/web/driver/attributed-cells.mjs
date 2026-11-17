import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
/** Cost: driver once; visits its own fixture and helper-built boxes, no application ledgers. */
export async function runAttributedCells(client,navigate) {
  await navigate('index.html');
  assert.equal(await client.evaluate('fixture._fixture_configure(-600,0);web.start()'),0);
  const result=await client.evaluate(`(async()=>{
    const check=(yes,why)=>{if(!yes) throw Error(why)};
    const api=fixture.textTest;
    const resource=[...document.querySelectorAll('[data-loka-token]')].find(e=>api.runsRow(+e.dataset.lokaToken).kind===7);
    check(resource,'kind 7 mounted');
    const row=api.runsRow(+resource.dataset.lokaToken), words=row.runs.words;
    check(resource.children.length===words[1],'fragment count');
    check(row.listener===null,'no attributed listener');
    const saved=[...resource.children];
    const root=resource.closest('[data-loka-token]')===resource?resource.parentElement:resource;
    web.resize(+root.dataset.lokaToken,180,120);
    check(saved.every((e,i)=>e===resource.children[i]),'equal payload child identity');
    for(let i=0;i<words[1];++i) {
      const o=3+7*i,e=resource.children[i],r=e.getBoundingClientRect(),parent=resource.getBoundingClientRect();
      check(r.left-parent.left===words[o+1] && r.top-parent.top===words[o+2], 'integer position');
      check(r.width===words[o+3] && r.height===words[o+4],'integer extent');
    }
    check(getComputedStyle(resource.children[0]).color==='rgb(255, 0, 0)','red');
    check(getComputedStyle(resource.children[1]).color==='rgb(0, 0, 255)','blue');
    check(getComputedStyle(resource.children[1]).textDecorationLine==='underline','underline');
    const face=new FontFace('Loka Text Probe','url(text-probe.ttf)');document.fonts.add(await face.load());
    const service=api.service(),ctx=service.ctx;
    const parent=document.createElement('div');
    parent.style.cssText='position:absolute;left:0;top:150px;text-align:right;text-align-last:justify;line-height:90px;letter-spacing:8px;word-spacing:9px;direction:rtl;text-transform:uppercase;';
    document.body.appendChild(parent);
    let baselineChecks=0;
    for(let key=0;key<48;++key) {
      const env=service.environment(key);
      const probeService={environment:()=>({applyToElement(el){env.applyToElement(el);el.style.fontFamily='"Loka Text Probe"'}})};
      env.applyToCanvas(ctx);ctx.font=ctx.font.replace(/system-ui|monospace/,'"Loka Text Probe"');
      const a=ctx.measureText('A'),v=ctx.measureText('V'),pair=ctx.measureText('AV');
      check(pair.width<a.width+v.width-0.1,'nonadditive shaping positive control '+key);
      const A=Math.ceil(a.fontBoundingBoxAscent),D=Math.ceil(a.fontBoundingBoxDescent),H=A+D;
      const descriptor={sizeKind:1,sizePayload:[9,10,12,14,18,24][Math.floor(key/8)],
        weight:key&4?1:0,italic:key&2?1:0,family:key&1?2:1,color:5,underline:1};
      const boxes=[];
      for(const [text,left,m] of [['A',0,a],['V',Math.ceil(a.width),v]]) {
        const box=api.fragmentBox([0,left,0,Math.ceil(m.width),H],0,descriptor,text,probeService);
        parent.appendChild(box);boxes.push(box);
        const range=document.createRange();range.selectNodeContents(box);
        check(Math.abs(range.getBoundingClientRect().width-m.width)<1,'raw advance '+key);
        check(box.getBoundingClientRect().width===Math.ceil(m.width),'allocated ceil '+key);
        const css=getComputedStyle(box);
        check(css.textAlign==='left' && css.textAlignLast==='auto' && (css.letterSpacing==='normal' || css.letterSpacing==='0px')
          && css.direction==='ltr' && css.lineHeight===H+'px','hostile inherited typography '+key);
        const marker=document.createElement('span');
        marker.style.cssText='display:inline-block;width:0;height:0;padding:0;border:0;vertical-align:baseline';
        box.appendChild(marker);
        const baseline=marker.getBoundingClientRect().top-box.getBoundingClientRect().top;
        check(Math.abs(baseline-A)<=0.5,'conditional baseline '+key);++baselineChecks;
      }
      check(boxes[1].getBoundingClientRect().left-boxes[0].getBoundingClientRect().left===Math.ceil(a.width),'isolated fragment shaping');
      boxes.forEach(e=>e.remove());
    }
    // Real mixed family/size Envs: align the allocated baselines, then observe CSS.
    const keys=[0,41], metrics=keys.map(key=>{
      service.environment(key).applyToCanvas(ctx);
      const m=ctx.measureText('x');return {A:Math.ceil(m.fontBoundingBoxAscent),D:Math.ceil(m.fontBoundingBoxDescent)};
    });
    const maxA=Math.max(...metrics.map(m=>m.A));
    const baselines=[];
    keys.forEach((key,i)=>{
      const m=metrics[i], descriptor={sizeKind:1,sizePayload:i?24:9,weight:0,italic:0,family:i?2:1,color:3,underline:0};
      const box=api.fragmentBox([0,i*40,maxA-m.A,30,m.A+m.D],0,descriptor,'x',service);
      parent.appendChild(box);
      const marker=document.createElement('span');
      marker.style.cssText='display:inline-block;width:0;height:0;padding:0;border:0;vertical-align:baseline';
      box.appendChild(marker);
      const baseline=marker.getBoundingClientRect().top-parent.getBoundingClientRect().top;
      check(Math.abs(baseline-maxA)<=0.5,'mixed family conditional baseline');baselines.push(baseline);
    });
    check(Math.abs(baselines[0]-baselines[1])<=1,'mixed family baseline difference');
    parent.remove();
    // Mounted snapshot replacement, mixed-size baseline boxes and empty/break-only paths.
    fixture._fixture_attributed_value(1);
    const mixed=row.runs.words;
    for(let i=0;i<mixed[1];++i) {
      const o=3+7*i, child=resource.children[i], r=child.getBoundingClientRect(), pr=resource.getBoundingClientRect();
      check(r.left-pr.left===mixed[o+1] && r.top-pr.top===mixed[o+2]
        && r.width===mixed[o+3] && r.height===mixed[o+4],'mixed mounted geometry');
    }
    const mixedSelection=getSelection(),mixedRange=document.createRange();mixedRange.selectNodeContents(resource);
    mixedSelection.removeAllRanges();mixedSelection.addRange(mixedRange);
    const mixedObservation={sourceLines:['large small','next'],textContent:resource.textContent,selection:mixedSelection.toString()};
    mixedSelection.removeAllRanges();
    fixture._fixture_attributed_value(2);
    check(resource.children.length===0 && resource.getBoundingClientRect().height>0,'break-only snapshot');
    fixture._fixture_attributed_value(3);
    check(resource.children.length===0,'empty snapshot');
    fixture._fixture_attributed_value(0);
    const selection=getSelection(),range=document.createRange();range.selectNodeContents(resource);
    selection.removeAllRanges();selection.addRange(range);
    const observation={textContent:resource.textContent,selection:selection.toString(),baselineChecks,mixed: mixedObservation};selection.removeAllRanges();
    return observation;
  })()`);
  assert.equal(result.textContent,'redblue');assert.equal(result.baselineChecks,96);
  console.log('Attributed selection observation:',JSON.stringify(result));
  await client.evaluate('fixture._fixture_attributed_value(1)');
  const ax=await client.send('Accessibility.getFullAXTree');
  console.log('Attributed accessibility observation:',JSON.stringify(ax.nodes.filter(n=>n.role?.value==='StaticText').map(n=>n.name?.value)));
  const capture=await client.send('Page.captureScreenshot',{format:'png'});
  await writeFile('build/web-attributed.png',Buffer.from(capture.data,'base64'));
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
  console.log('Attributed DOM: mounted boxes, paint, identity, 48 fonts, shaping and baselines passed');
}
