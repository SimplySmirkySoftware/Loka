import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
/** Cost: once per driver run, visits this fixture's four projected leaves. */
export async function runDocumentCells(client, navigate) {
  await navigate('index.html');
  assert.equal(await client.evaluate('fixture._fixture_configure(-700,0);web.start()'),0);
  const result = await client.evaluate(`(()=>{
    const check=(condition,message)=>{if(!condition)throw Error(message)};
    const api=fixture.textTest;
    const leaves=[...document.querySelectorAll('[data-loka-token]')]
      .filter(e=>api.runsRow(+e.dataset.lokaToken).kind===7);
    check(leaves.length===4,'four attributed leaves');
    const [heading,paragraph,code,after]=leaves;
    check(heading.textContent==='Title codelink','heading inline content');
    check(paragraph.textContent==='paragraph','paragraph content');
    check(code.textContent==='firstsecond','explicit code lines');
    const runs=api.runsRow(+heading.dataset.lokaToken);
    check(runs.listener===null,'Web links remain nonactivating');
    check(parseFloat(getComputedStyle(heading.children[0]).fontSize)>parseFloat(getComputedStyle(paragraph.children[0]).fontSize),'heading size');
    check(getComputedStyle(heading.children[0]).color==='rgb(255, 0, 0)','segment color wins');
    const segments=[...heading.children];
    const inlineCode=segments.find(e=>e.textContent==='code'), link=segments.find(e=>e.textContent==='link');
    check(inlineCode && getComputedStyle(inlineCode).fontFamily.includes('monospace'),'inline code family');
    check(link && getComputedStyle(link).textDecorationLine==='underline','link style');
    const rectangles=leaves.map(e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}});
    check(rectangles[0].x===rectangles[1].x && rectangles[2].x-rectangles[1].x===3,'existing Card content inset');
    check(document.querySelectorAll('[data-loka-token]').length===9,'one window, four leaves and four frame strips');
    const root=heading.parentElement;
    check(root.children.length===8 && leaves.every(e=>e.parentElement===root),'leaves and frame share the root');
    const strips=[...root.children].filter(e=>api.runsRow(+e.dataset.lokaToken).kind===8);
    check(strips.length===4 && strips.every(e=>getComputedStyle(e.firstElementChild).borderTopStyle==='solid'),'Card frame paint');
    check(getComputedStyle(code).boxShadow==='none','no shadow drawn');
    check(rectangles[1].width-rectangles[2].width===9,'horizontal Card allowances');
    check(rectangles[2].height>0 && rectangles[3].y-rectangles[2].y-rectangles[2].height===9,'gap after framed code');
    for(const leaf of leaves) {
      const row=api.runsRow(+leaf.dataset.lokaToken), words=row.runs.words;
      const parent=leaf.getBoundingClientRect();
      check(leaf.children.length===words[1],'fragment count');
      for(let i=0;i<words[1];++i) {
        const o=3+7*i,r=leaf.children[i].getBoundingClientRect();
        check(r.x-parent.x===words[o+1] && r.y-parent.y===words[o+2]
          && r.width===words[o+3] && r.height===words[o+4],'allocated DOM geometry');
      }
      check(getComputedStyle(leaf).borderTopWidth==='0px','unframed projection');
    }
    return {rectangles,bandSeatGap:rectangles[3].y-rectangles[2].y-rectangles[2].height};
  })()`);
  const capture=await client.send('Page.captureScreenshot',{format:'png'});
  await writeFile('build/web-document.png',Buffer.from(capture.data,'base64'));
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
  await writeFile('build/web-document-geometry.json',JSON.stringify(result,null,2));
  console.log('document vocabulary DOM and geometry passed',result);
}
