import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';

// Cost: driver once per pair; decode and compare both cropped screenshots at their native pixel size.
async function pixelDifference(client, assembled, reference) {
  return client.evaluate(`(async()=>{
    async function decode(data) {
      const image=new Image();
      image.src='data:image/png;base64,'+data;
      await image.decode();
      const canvas=document.createElement('canvas');
      canvas.width=image.naturalWidth; canvas.height=image.naturalHeight;
      const ctx=canvas.getContext('2d');
      ctx.drawImage(image,0,0);
      return ctx.getImageData(0,0,canvas.width,canvas.height);
    }
    const [a,b]=await Promise.all([decode('${assembled}'),decode('${reference}')]);
    if(!a.width || !a.height || a.width!==b.width || a.height!==b.height) throw Error('capture dimensions');
    let max=0, changedPixels=0, referenceHasInk=false; const worst=[];
    for(let i=0;i<a.data.length;i+=4) {
      let diff=0;
      for(let channel=0;channel<4;++channel)
        diff=Math.max(diff,Math.abs(a.data[i+channel]-b.data[i+channel]));
      if(b.data[i]!==b.data[0] || b.data[i+1]!==b.data[1] || b.data[i+2]!==b.data[2]) referenceHasInk=true;
      max=Math.max(max,diff);
      if(diff) ++changedPixels;
      if(diff>8 && worst.length<400) worst.push([(i/4)%a.width,Math.floor(i/4/a.width),diff]);
    }
    if(!referenceHasInk) throw Error('reference crop is a solid colour');
    return {max,changedPixels,width:a.width,height:a.height,worst:JSON.stringify(worst)};
  })()`);
}

/** Cost: driver once; eight mounts, four strip inspections and two region captures per mount. */
export async function runCardCells(client, navigate) {
  const failures=[];
  try {
    for(const scale of [1,1.5]) {
      await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:scale,mobile:false});
      for(const wide of [false,true]) for(const minimum of [true,false]) {
        const name=`${wide?'WIDE':'DROP'}-${minimum?'min':'large'}-x${scale}`;
        const width=minimum ? (wide?60:17) : 100;
        const height=minimum ? (wide?68:17) : 80;
        const mode=-800-(wide?2:0)-(minimum?0:1);
        await navigate('index.html');
        const clip=await client.evaluate(`(()=>{
          const listened=new WeakSet(), add=EventTarget.prototype.addEventListener;
          EventTarget.prototype.addEventListener=function(...args) {
            listened.add(this);
            return add.apply(this,args);
          };
          try {
            fixture._fixture_configure(${mode},0);
            if(web.start()!==0) throw Error('Card fixture start');
          } finally { EventTarget.prototype.addEventListener=add; }

          const root=document.querySelector('body > [data-loka-token]');
          const bands=[...root.children];
          const w=${width},h=${height},wide=${wide};
          const [left,top,right,bottom]=wide?[15,11,15,19]:[3,3,6,6];
          const strips=[[0,0,w-right,top],[0,top,left,h-top],[w-right,0,right,h-bottom],[left,h-bottom,w-left,bottom]];
          const outer=root.getBoundingClientRect();
          const check=(ok,msg)=>{if(!ok)throw Error('${name} '+msg)};
          check(devicePixelRatio===${scale},'device scale');
          check(bands.length===4,'four strips');
          const reference=document.createElement('div'), body=document.createElement('div');
          Object.assign(reference.style,{position:'absolute',left:'0',top:'0',width:w+'px',height:h+'px',
            overflow:'hidden',visibility:'hidden'});
          Object.assign(body.style,{position:'absolute',left:(wide?12:0)+'px',top:(wide?8:0)+'px',
            width:(w-(wide?24:3))+'px',height:(h-(wide?24:3))+'px',boxSizing:'border-box',
            border:'1px solid GrayText',borderRadius:'6px',background:'transparent',
            boxShadow:wide?'0 4px 8px rgba(0,0,0,.25)':'3px 3px 0 rgba(0,0,0,.5)'});
          reference.appendChild(body);
          root.appendChild(reference);
          const expected=getComputedStyle(body);
          for(const [i,band] of bands.entries()) {
            const row=fixture.textTest.runsRow(+band.dataset.lokaToken), child=band.firstElementChild;
            check(row.kind===8 && band.childElementCount===1,'band kind and child');
            check(row.listener===null && !listened.has(band) && !listened.has(child),'listener');
            const r=band.getBoundingClientRect(),s=getComputedStyle(band),paint=getComputedStyle(child);
            check([r.x-outer.x,r.y-outer.y,r.width,r.height].every((v,j)=>v===strips[i][j]),'strip '+i);
            check(s.overflow==='hidden' && s.pointerEvents==='none' && s.visibility==='visible','clip and hit test');
            for(const property of ['borderTopWidth','borderTopStyle','borderTopColor','borderRadius','backgroundColor','boxShadow'])
              check(paint[property]===expected[property],property+' on strip '+i);
          }
          window.showCardReference=()=>{
            for(const band of bands) band.style.visibility='hidden';
            reference.style.visibility='visible';
          };
          return {x:outer.x+scrollX,y:outer.y+scrollY,width:w,height:h,scale:1};
        })()`);
        const capture=async()=>(await client.send('Page.captureScreenshot',{format:'png',clip})).data;
        const assembled=await capture();
        await client.evaluate('showCardReference()');
        const reference=await capture();
        const result=await pixelDifference(client,assembled,reference);
        console.log('Card paint',name,result);
        assert(Math.abs(result.width-width*scale)<=1 && Math.abs(result.height-height*scale)<=1,
          name+' physical crop dimensions');
        await writeFile(`build/web-card-${name}.png`,Buffer.from(assembled,'base64'));
        // Measured in Chromium 157: straight runs agree within 8; the strip joins cross the corner arcs, where
        // clip-edge antialiasing differs (<= 10 at scale 1, <= 36 at 1.5). A missing border run differs by > 100.
        const bx=wide?12:0, by=wide?8:0, bw=width-(wide?24:3), bh=height-(wide?24:3);
        const nearCorner=([x,y])=>[bx,bx+bw].some(cx=>Math.abs(x/scale-cx)<=8)
          && [by,by+bh].some(cy=>Math.abs(y/scale-cy)<=8);
        const stray=JSON.parse(result.worst).filter(pixel=>!nearCorner(pixel));
        if(result.max>(Number.isInteger(scale)?16:48) || stray.length) failures.push({name,...result,stray});
        await client.evaluate('web.dispose();web.completed.then(()=>true)');
        assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
      }
    }
  } finally {
    await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  }
  assert.deepEqual(failures,[],'Card paint: differences above 8 only at the corner joins, within the measured bound');
}
