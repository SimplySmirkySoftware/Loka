#!/usr/bin/env node
import {spawn, spawnSync} from 'node:child_process';
import {createServer} from 'node:http';
import {readFile, mkdtemp, rm} from 'node:fs/promises';
import {resolve, join, extname, basename} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const driverDir=fileURLToPath(new URL('.',import.meta.url));
import assert from 'node:assert/strict';
import {runStackCells, runHelloRoundTrip} from './stack-cells.mjs';
import {runSkinCells} from './skin-cells.mjs';
import {runCellCells} from './cell-cells.mjs';
import {runGridCells} from './grid-cells.mjs';
import {renderPreviewHtml} from '../../../tools/vscode-loka/src/previewHtml.ts';

const build=resolve(process.argv[2] || 'build/wasm/testing/web');
const mineBuild=resolve('build/wasm/web/example/MineSweeper');
const helloBuild=resolve('build/wasm/web/example/HelloWorld');
const chromium=process.env.CHROMIUM || ['chromium','chromium-browser','google-chrome']
  .find(name=>spawnSync('which',[name],{stdio:'ignore'}).status===0);
if(!chromium) { console.log('[skip] browser driver: no chromium'); process.exit(0); }
if(typeof WebSocket==='undefined') throw new Error('This Node has no built-in WebSocket client; use the emsdk Node.');
// Emscripten routes wasm stderr to console.error. The debug build's educational
// diagnostic (common/platform/debug/DebugLog.hpp) about unobserved private state is
// expected from the counter fixture's count_ and is not a browser error.
const isEducationalDiagnostic=message=>message.params.args.length===1
  && typeof message.params.args[0].value==='string'
  && message.params.args[0].value.startsWith('[scene-update] committed StateBase ');
/** Minimal raw CDP client. Every request has a bounded response deadline. */
class CDP {
  constructor(socket) {
    this.socket=socket; this.next=1; this.pending=new Map(); this.errors=[];
    socket.onmessage=({data})=>{
      const message=JSON.parse(data);
      if(message.id) {
        const pending=this.pending.get(message.id); if(!pending) return;
        clearTimeout(pending.timer); this.pending.delete(message.id);
        if(message.error) pending.reject(new Error(JSON.stringify(message.error)));
        else pending.resolve(message.result);
      } else if(message.method==='Runtime.exceptionThrown'
          || (message.method==='Runtime.consoleAPICalled' && message.params.type==='error'
              && !isEducationalDiagnostic(message)))
        this.errors.push(message);
    };
  }
  static async connect(url) {
    const socket=new WebSocket(url);
    await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
    return new CDP(socket);
  }
  send(method,params={}) {
    const id=this.next++;
    return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error('CDP timeout: '+method));},10000);
      this.pending.set(id,{resolve,reject,timer}); this.socket.send(JSON.stringify({id,method,params}));
    });
  }
  async evaluate(expression) {
    const result=await this.send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
    if(result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
}
assert.equal(spawnSync('python3',[join(driverDir,'make-font.py'),join(build,'text-probe.ttf')],{stdio:'inherit'}).status,0);
assert.equal(spawnSync('python3',[join(driverDir,'make-font.py'),join(build,'stack-probe.ttf'),'--ascii-labels'],{stdio:'inherit'}).status,0);
const profile=await mkdtemp(join(tmpdir(),'loka-web-chrome-'));
let helloWasmFetches=0;
const server=createServer(async(req,res)=>{
  try {
    const file=basename(new URL(req.url,'http://localhost').pathname) || 'index.html';
    let data;
    const mode = new URL(req.url, 'http://localhost').searchParams.get('cell');
    if(file === 'preview.html') {
      data = renderPreviewHtml({kind:'module', id:'preview-document', nonce:'preview-nonce',
        cspSource: "'self'", exportName: mode === 'mine' ? 'LokaMineWeb' : 'LokaHelloWeb',
        jsUri: mode === 'mine' ? 'preview-mine.js' : mode === 'script-fail' ? 'missing-preview.js' : 'preview-module.js',
        wasmUri: mode === 'mine' ? 'LokaMineWeb.wasm' : mode === 'wasm-fail' ? 'missing-preview.wasm' : 'LokaHelloWeb.wasm'});
      if(mode?.startsWith('nonce-')) {
        let index = 0;
        data = data.replace(/<script nonce="preview-nonce"/g, match =>
          index++ === Number(mode.slice(6)) ? '<script' : match);
      }
      if(mode === 'wasm-csp') data = data.replace(' &#39;wasm-unsafe-eval&#39;', '');
    } else if(file === 'missing-preview.js') {
      await new Promise(resolve => setTimeout(resolve, 100));
      res.writeHead(404); res.end(); return;
    } else if(file === 'csp-allow.html' || file === 'csp-deny.html') {
      const wasm=file === 'csp-allow.html' ? " 'wasm-unsafe-eval'" : '';
      res.setHeader('Content-Security-Policy',`script-src 'nonce-X'${wasm}; connect-src 'self'; default-src 'none'`);
      data=`<!doctype html><body><script nonce="X" src="LokaHelloWeb.js"></script>
        <script nonce="X">LokaHelloWeb({locateFile:name=>name,lokaSkin:new URLSearchParams(location.search).get('skin')}).then(m=>{
          window.fixture=m;window.web=m.web;window.ready=true;
        }).catch(error=>{window.loadError=String(error)});</script>`;
    } else if(file === 'mine.html') {
      data=`<!doctype html><body><script src="LokaMineWeb.js"></script><script>
        LokaMineWeb({locateFile:name=>name,lokaSkin:new URLSearchParams(location.search).get('skin')}).then(m=>{window.fixture=m;window.web=m.web;window.ready=true});
        </script>`;
    } else if(file === 'LokaMineWeb.wasm') data=await readFile(join(mineBuild,file));
    else if(file === 'hello.html') {
      data=`<!doctype html><body><script src="LokaHelloWeb.js"></script><script>
        LokaHelloWeb({locateFile:name=>name,lokaSkin:new URLSearchParams(location.search).get('skin')}).then(m=>{window.fixture=m;window.web=m.web;window.ready=true});
        </script>`;
    } else if(file === 'LokaHelloWeb.wasm') data=await readFile(join(helloBuild,file));
    else if(file === 'text-cells.js') data=await readFile(join(driverDir,file));
    else if(file === 'LokaMineWeb.js' || file === 'skew.js' || file === 'LokaWebCounter.js' || file === 'LokaHelloWeb.js' || file === 'preview-module.js' || file === 'preview-mine.js') {
      data=await readFile((file === 'LokaMineWeb.js' || file === 'preview-mine.js') ? join(mineBuild,'LokaMineWeb.js') : (file === 'LokaHelloWeb.js' || file === 'preview-module.js') ? join(helloBuild,'LokaHelloWeb.js') : join(build,'LokaWebCounter.js'),'utf8');
      // Driver-only closure access, never a production export or a C++ test-shaped door.
      const hook=`Module['textTest']={
        service:()=>WebDOM.text,
        replaceService:service=>{WebDOM.text=service},
        resources:()=>WebDOM.resources.size,
        immediatePending:()=>[...WebDOM.tasks.values()].some(row=>row.kind===2),
        admit:()=>LokaWebText.admit(LokaWebStyle.wire.fingerprint,LokaWebStyle.wire.count,
          document,navigator,CSS,message=>console.error(message)),
        badDescriptor:token=>_web_set_text_style(token,0,8)
      };
`;
      // Match the API assignment in Debug and minified Release output alike.
      const api=/Module\[['"]web['"]\]\s*=/g;
      assert.equal([...data.matchAll(api)].length,1,'one module API assignment');
      data=data.replace(api,match=>hook+match);
      if(file === 'preview-module.js' || file === 'preview-mine.js') {
        const factoryName = file === 'preview-mine.js' ? 'LokaMineWeb' : 'LokaHelloWeb';
        data += `
        // Driver-only observation of the real module; not a shipped shell API.
        { const factory = ${factoryName};
          ${factoryName} = options => {
            const cell = new URL(location.href).searchParams.get('cell');
            if(cell === 'throw') throw new Error('driver factory throw');
            return factory(options).then(async module => {
              window.observedModule = module;
              window.startCalls = 0;
              const start = module.web.start;
              module.web.start = () => { ++window.startCalls; return start(); };
              module.web.completed.then(() => { window.railSettled = true; });
              if(cell === 'pending') await new Promise(resolve => setTimeout(resolve, 100));
              return module;
            });
          };
        }
      `;
      }
      if(file === 'skew.js') data=data.replace(/"fingerprint":\d+/, '"fingerprint":0');
    } else data=await readFile(join(build,file));
    const type={'.html':'text/html','.js':'application/javascript','.wasm':'application/wasm'}[extname(file)];
    res.writeHead(200,{'Content-Type':type || 'application/octet-stream'});res.end(data);
    if(file === 'LokaHelloWeb.wasm') ++helloWasmFetches;
  } catch {res.writeHead(404);res.end();}
});
await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve)});
const child=spawn(chromium,['--headless','--no-sandbox','--disable-gpu','--remote-debugging-port=0',
  '--user-data-dir='+profile,'about:blank'],{stdio:['ignore','ignore','pipe']});
let client;
try {
  const browserURL=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('Chromium startup timeout')),10000);
    let stderr=''; child.stderr.on('data',data=>{stderr+=data;const match=stderr.match(/DevTools listening on (ws:\/\/\S+)/);
      if(match){clearTimeout(timer);resolve(match[1]);}});
    child.on('error',reject);
  });
  const origin=browserURL.replace(/^ws:/,'http:').replace(/\/devtools\/.*$/,'');
  const pages=await (await fetch(origin+'/json/list')).json();
  client=await CDP.connect(pages.find(page=>page.type==='page').webSocketDebuggerUrl);
  await client.send('Runtime.enable'); await client.send('Page.enable');
  await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  console.log(JSON.stringify({engine:await client.send('Browser.getVersion'),os:process.platform,node:process.version,
    revision:spawnSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).stdout.trim(),viewport:[800,600]}));
  async function navigate(page) {
    // A navigation acknowledgement can precede replacement of the old document.
    await client.evaluate('window.ready=false;window.loadError=null');
    await client.send('Page.navigate',{url:'http://127.0.0.1:'+server.address().port+'/'+page});
    for(let i=0;i<100 && !await client.evaluate('!!window.ready || !!window.loadError');++i)
      await new Promise(resolve=>setTimeout(resolve,50));
  }
  const injection = await client.send('Page.addScriptToEvaluateOnNewDocument', {source: `
    window.messages = [];
    window.railSettled = false;
    window.acquireVsCodeApi = () => {
      const cell = new URL(location.href).searchParams.get('cell');
      if(['pending','script-fail','throw'].includes(cell)) queueMicrotask(() =>
        dispatchEvent(new MessageEvent('message', {data:{id:'preview-document',type:'dispose'}})));
      return {postMessage: message => messages.push({...message, settled:railSettled})};
    };
  `});
  async function previewWait(expression, expected = true) {
    for(let i=0; i<100; ++i) {
      if(await client.evaluate(expression) === expected) return;
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    assert.equal(await client.evaluate(expression), expected, expression);
  }
  const disposePreview = () => client.evaluate(`dispatchEvent(new MessageEvent('message',
    {data:{id:'preview-document',type:'dispose'}}))`);
  for(const cell of ['normal','mine','pending','wasm-fail','script-fail','throw',
      'nonce-0','nonce-1','nonce-2','wasm-csp']) {
    client.errors.length = 0;
    await client.send('Page.navigate', {url:'http://127.0.0.1:'+server.address().port+'/preview.html?cell='+cell});
    await previewWait(`location.search === '?cell=${cell}' && Array.isArray(window.messages)`);
    if(cell.startsWith('nonce-') || cell === 'wasm-csp') {
      await new Promise(resolve => setTimeout(resolve, 1000));
      assert.equal(await client.evaluate(`messages.filter(m=>m.type==='started').length`), 0);
    } else if(['wasm-fail','script-fail','throw'].includes(cell)) {
      await previewWait(`messages.some(m=>m.type==='error')`);
      if(cell === 'wasm-fail') await disposePreview();
      await previewWait(`messages.filter(m=>m.type==='absent').length === 1`);
      await disposePreview();
      assert.equal(await client.evaluate(`messages.filter(m=>m.type==='absent').length`), 1);
      assert.equal(await client.evaluate(`messages.filter(m=>m.type==='completed').length`), 0);
    } else {
      if(cell === 'normal' || cell === 'mine') {
        await previewWait(`messages.some(m=>m.type==='started' && m.status===0)`);
        assert.equal(await client.evaluate(`messages.filter(m=>m.type==='completed').length`), 0);
        assert.equal(await client.evaluate('railSettled'), false);
        if(cell === 'mine') {
          assert.equal(await client.evaluate("document.querySelectorAll('button').length"), 65);
          assert.equal(await client.evaluate('observedModule.textTest.resources() > 0'), true);
          await client.evaluate("[...document.querySelectorAll('button')].find(e=>e.textContent==='New Game').click()");
          assert.equal(await client.evaluate("document.querySelectorAll('button').length"), 65);
        } else {
          assert.equal(await client.evaluate("document.body.textContent.includes('clicks: 0')"), true);
          await client.evaluate("[...document.querySelectorAll('button')].find(e=>e.textContent==='Probe Button').click()");
          await previewWait("document.body.textContent.includes('clicks: 1')");
        }
        await disposePreview();
      }
      await previewWait(`messages.filter(m=>m.type==='completed').length === 1`);
      assert.equal(await client.evaluate('startCalls'), ['normal', 'mine'].includes(cell) ? 1 : 0);
      assert.equal(await client.evaluate('observedModule.textTest.resources()'), 0);
      assert.deepEqual(client.errors, [], 'preview shell runtime errors');
      assert.equal(await client.evaluate(`messages.filter(m=>m.type==='completed' && !m.settled).length`), 0);
      await disposePreview();
      assert.equal(await client.evaluate(`messages.filter(m=>m.type==='completed').length`), 1);
    }
    console.log('preview shell '+cell+': passed');
  }
  await client.send('Page.removeScriptToEvaluateOnNewDocument', {identifier:injection.identifier});
  client.errors.length = 0; // Negative CSP/load cells deliberately produce errors.
  await navigate('index.html');
  await client.evaluate('document.body.style.direction="rtl";fixture._fixture_configure(-1,0);web.start()');
  const scrollInfo=await client.evaluate(`(() => {
    const root=document.querySelector('body > div[data-loka-token]'),s=root.firstElementChild,c=s.firstElementChild;
    window.scrollCell={root,s,c};
    return {native:s.scrollTop,fact:fixture._fixture_scroll_fact(1),vertical:s.offsetWidth-s.clientWidth,horizontal:s.offsetHeight-s.clientHeight,
      width:s.clientWidth,reportedWidth:fixture._fixture_scroll_fact(2),document:parseFloat(c.style.width)};
  })()`);
  assert.equal(scrollInfo.native,scrollInfo.fact+7,'bootstrap nonzero origin after reveal');
  assert.equal(scrollInfo.width,scrollInfo.reportedWidth);
  assert.equal(scrollInfo.document,400);
  console.log('scroll gutter: '+JSON.stringify(scrollInfo));
  console.log(scrollInfo.vertical ? 'classic gutter: passed; [skip] overlay gutter: not active' : 'overlay gutter: passed; [skip] classic gutter: not active');
  await client.evaluate('scrollCell.s.scrollTop=70;scrollCell.s.dispatchEvent(new Event("scroll"))');
  assert.equal(await client.evaluate('fixture._fixture_scroll_fact(1)'),63,'live scroll sampling');
  await client.evaluate('fixture._fixture_scroll_command(0,0,0)');
  assert.equal(await client.evaluate('scrollCell.s.scrollTop'),0,'scrollTo Top');
  for(const command of [2,3]) {
    const mismatch=await client.evaluate(`(() => {
      scrollCell.s.scrollTop=80;
      const differs=Math.round(scrollCell.s.scrollTop)!==fixture._fixture_scroll_fact(1)+7;
      fixture._fixture_scroll_command(${command},0,Number(scrollCell.root.dataset.lokaToken));
      fixture._fixture_scroll_command(${command},1,Number(scrollCell.root.dataset.lokaToken));
      return {differs,native:Math.round(scrollCell.s.scrollTop),fact:fixture._fixture_scroll_fact(1)+7};
    })()`);
    assert(mismatch.differs,'native/fact mismatch created while a layout box exists');
    assert.equal(mismatch.native,mismatch.fact,'reveal restores native/fact agreement before a queued event can publish');
  }
  await client.evaluate('fixture._fixture_scroll_command(1,0,0)');
  assert.equal(await client.evaluate('scrollCell.s.style.overflowX'),'hidden','narrow disables horizontal bar');
  await client.evaluate('fixture._fixture_scroll_command(1,400,0)');
  assert.equal(await client.evaluate('scrollCell.s.style.overflowX'),'scroll','wide enables horizontal bar');
  await client.evaluate('scrollCell.s.scrollTop=99999;scrollCell.s.dispatchEvent(new Event("scroll"))');
  assert.equal(await client.evaluate('Math.round(scrollCell.s.scrollTop)'),await client.evaluate('fixture._fixture_scroll_fact(1)+7'),'range boundary');
  const failed=await client.evaluate(`(() => {
    const snapshot=()=>[scrollCell.s.scrollHeight,scrollCell.c.scrollTop,...[0,1,2,3].map(i=>fixture._fixture_scroll_fact(i))];
    const before=snapshot();window.savedScrollService=fixture.textTest.service();
    // Measurement-only failure: a lost canvas context makes web_measure REFUSED while the style door still answers (a null service would be a protocol refusal, not a text refusal).
    fixture.textTest.replaceService(Object.create(savedScrollService,{ctx:{value:Object.create(savedScrollService.ctx,{isContextLost:{value:()=>true}})}}));
    fixture._fixture_scroll_command(4,12345,0);
    return {before,after:snapshot()};
  })()`);
  assert.deepEqual(failed.after,failed.before,'failed traversal preserves range, content offset and fact');
  await client.evaluate('fixture.textTest.replaceService(savedScrollService);fixture._web_wake(2,performance.now())');
  const fraction=await client.evaluate(`(() => {
    scrollCell.s.style.zoom='1.25';scrollCell.s.scrollTop=10.8;
    const before=scrollCell.s.scrollTop;scrollCell.s.dispatchEvent(new Event('scroll'));
    const afterInput=scrollCell.s.scrollTop;
    fixture._web_wake(2,performance.now());
    return {before,afterInput,after:scrollCell.s.scrollTop,fact:fixture._fixture_scroll_fact(1)};
  })()`);
  assert.equal(Math.round(fraction.after),fraction.fact+7,'fractional read-back publication');
  assert.equal(fraction.afterInput,fraction.before,'scroll input does not snap fractional native origin');
  assert.equal(fraction.after,fraction.before,'stable rounded origin does not snap');
  await client.evaluate('scrollCell.s.style.zoom="1"');
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('fixture.textTest.resources()'),0,'scroll release subtree rows');
  console.log('scroll browser: bootstrap, live input, request, reveal, ranges and release passed');
  await navigate('index.html');
  await client.evaluate('fixture._fixture_configure(-2,0);web.start()');
  const closingScroll=await client.evaluate(`(() => {
    const roots=[...document.querySelectorAll('body > div[data-loka-token]')];
    return {count:roots.length,result:fixture._fixture_scroll_close(Number(roots[0].dataset.lokaToken),Number(roots[0].firstElementChild.dataset.lokaToken))};
  })()`);
  assert.equal(closingScroll.count,2);assert.equal(closingScroll.result,5,'pending-close root refuses scroll while another root remains');
  await client.evaluate('web.dispose();web.completed.then(()=>true)');
  await runGridCells(client, navigate);
  await runCellCells(client, navigate);
  await runSkinCells(client, navigate);
  await runStackCells(client, navigate);
  await runHelloRoundTrip(client, navigate);
  for(const viewportWidth of [800,300]) {
    await client.send('Emulation.setDeviceMetricsOverride',{width:viewportWidth,height:600,deviceScaleFactor:1,mobile:false});
    await navigate('hello.html');
    assert.equal(await client.evaluate('!!window.ready'),true,'HelloWeb module ready');
    assert.equal(await client.evaluate('web.start()'),0,'HelloWeb start');
    const hello=await client.evaluate(`(() => {
      const roots=[...document.querySelectorAll('body > div[data-loka-token]')];
      const root=roots[0], css=getComputedStyle(root);
      const content=axis=>parseFloat(css[axis])-(css.boxSizing==='border-box' ?
        (axis==='width' ? parseFloat(css.paddingLeft)+parseFloat(css.paddingRight)+parseFloat(css.borderLeftWidth)+parseFloat(css.borderRightWidth) :
          parseFloat(css.paddingTop)+parseFloat(css.paddingBottom)+parseFloat(css.borderTopWidth)+parseFloat(css.borderBottomWidth)) : 0);
      return {roots:roots.length,children:root.childElementCount,width:content('width'),height:content('height'),available:document.body.clientWidth};
    })()`);
    // ScrollView owns one scroller and its content owns all projected leaves.
    assert.equal(hello.roots,1);assert.equal(hello.children,1);
    assert(hello.width>0 && hello.width<=420 && hello.height>0 && hello.height<=330);
    assert.equal(hello.width,Math.min(420,hello.available));assert.equal(hello.height,330);
    const leaves=await client.evaluate(`(() => {
      const scroller=document.querySelector('body > div[data-loka-token]').firstElementChild;
      return [...scroller.firstElementChild.children].map(e=>e.textContent);
    })()`);
    assert.equal(leaves.length,12,'HelloWeb projected leaf order count');
    for(const [index,label] of [[0,'Loka Sample'],[2,'Add +'],[4,'Probe Button'],[5,'Toggle Button Enabled'],[6,'Fruit Picker'],[8,'BMI Calculator'],[9,'Height (cm)'],[10,'Weight (kg)']])
      assert(leaves[index].includes(label),'HelloWeb leaf '+index);
    if(viewportWidth===800) {
      const hit=await client.evaluate(`(() => {
        const e=[...document.querySelectorAll('button')].find(e=>e.textContent==='Probe Button');
        const r=e.getBoundingClientRect(),s=e.parentElement.parentElement.getBoundingClientRect();
        const x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
        return {x,y,visible:r.top>=s.top&&r.bottom<=s.bottom&&r.width>0&&r.height>0,hit:document.elementFromPoint(x,y)===e};
      })()`);
      assert(hit.visible && hit.hit,'Probe Button visible and hit without scrolling');
      await client.send('Input.dispatchMouseEvent',{type:'mousePressed',x:hit.x,y:hit.y,button:'left',clickCount:1});
      await client.send('Input.dispatchMouseEvent',{type:'mouseReleased',x:hit.x,y:hit.y,button:'left',clickCount:1});
      assert(await client.evaluate(`document.body.textContent.includes('clicks: 1')`),'Probe Button increments clicks');
      const ring=await client.evaluate(`(() => {
        const style=document.createElement('style');style.textContent='button:focus{outline:3px solid rgb(255,0,255)!important;outline-offset:0!important}';document.head.appendChild(style);
        window.probeButton=[...document.querySelectorAll('button')].find(e=>e.textContent==='Probe Button');probeButton.blur();
        const r=probeButton.getBoundingClientRect();return {x:r.left+4,y:r.top-3,width:Math.max(1,r.width-8),height:2,scale:1};
      })()`);
      const unfocused=(await client.send('Page.captureScreenshot',{format:'png',clip:ring})).data;
      await client.evaluate('probeButton.focus()');
      const focused=(await client.send('Page.captureScreenshot',{format:'png',clip:ring})).data;
      assert.notEqual(focused,unfocused,'focus outline changes pixels outside button border');
      await client.evaluate('probeButton.blur()');
      const blurred=(await client.send('Page.captureScreenshot',{format:'png',clip:ring})).data;
      assert.equal(blurred,unfocused,'focus outline pixels disappear on blur');
      const clipHit=await client.evaluate(`(() => {
        const s=probeButton.parentElement.parentElement;
        s.scrollTop=probeButton.offsetTop+5;
        const r=probeButton.getBoundingClientRect(),c=s.getBoundingClientRect(),x=(r.left+r.right)/2;
        return {inside:document.elementFromPoint(x,Math.max(r.top,c.top)+3)===probeButton,
          outside:document.elementFromPoint(x,c.top-2)===probeButton};
      })()`);
      assert(clipHit.inside && !clipHit.outside,'focusable button hit is clipped at viewport edge');
    }
    await client.evaluate('web.dispose();web.completed.then(()=>true)');
    assert.equal(await client.evaluate('fixture.textTest.resources()'),0);
    assert.deepEqual(client.errors,[],'HelloWeb runtime errors');
    console.log('HelloWeb smoke: passed '+JSON.stringify({viewportWidth,...hello,resources:0,completed:true}));
  }
  await client.send('Emulation.setDeviceMetricsOverride',{width:800,height:600,deviceScaleFactor:1,mobile:false});
  for(const allow of [true,false]) {
    client.errors=[];
    const fetchesBefore=helloWasmFetches;
    await navigate(allow ? 'csp-allow.html' : 'csp-deny.html');
    assert(helloWasmFetches>fetchesBefore,'CSP wasm fetch succeeds');
    assert.equal(await client.evaluate('!!window.ready'),allow,'CSP module ready');
    if(allow) {
      assert.deepEqual(client.errors,[],'CSP allowed startup');
      await client.evaluate('web.dispose();web.completed.then(()=>true)');
    } else {
      assert.match(await client.evaluate('window.loadError || ""'),/wasm|WebAssembly|CompileError/i);
    }
    console.log('CSP '+(allow ? 'with' : 'without')+" wasm-unsafe-eval: passed (ready="+allow+')');
  }
  client.errors=[];
  await navigate('index.html');
  assert.equal(await client.evaluate('!!window.ready'),true,'module ready');
  // Inspect bootstrap synchronously, before ResizeObserver can repair a wrong box.
  const initial=await client.evaluate(`(() => {
    const style=document.createElement('style');
    style.id='loka-bootstrap-box-probe';
    style.textContent='body > div[data-loka-token] { padding:7px; border:3px solid; transform:scale(.9); }';
    document.head.appendChild(style);
    fixture._fixture_configure(2,0);
    const result=web.start();
    return {result,widths:[...document.querySelectorAll('body > div span')].map(e=>e.style.width)};
  })()`);
  assert.equal(initial.result,0);
  assert.deepEqual(initial.widths,['300px','300px']);
  const tokens=await client.evaluate(`({buttons:[...document.querySelectorAll('button')].map(e=>+e.dataset.lokaToken),
    roots:[...document.querySelectorAll('body > div')].map(e=>+e.dataset.lokaToken)})`);
  const button=tokens.buttons[0];
  assert.equal(tokens.roots.length,2,'configured roots=2');
  assert.equal(await client.evaluate('web.start()'),2,'second start refused');
  assert.deepEqual(await client.evaluate(`[...document.querySelectorAll('body > div[data-loka-token]')].map(e=>+e.dataset.lokaToken)`),tokens.roots);
  assert.equal(await client.evaluate('fixture._fixture_configured()'),20);
  await client.evaluate('fixture._fixture_configure(1,1)');
  assert.equal(await client.evaluate('fixture._fixture_configured()'),20,'configure after start is inert');
  console.log('fixture configure roots=2 and configure after start: passed');
  const text=()=>client.evaluate(`document.querySelector('[data-loka-token="${tokens.roots[0]}"] span').textContent`);
  await client.evaluate(`document.querySelector('[data-loka-token="${button}"]').click()`);
  assert.equal(await text(),'Count: 1');
  console.log('second start refusal preserves root token and click: passed');
  // Native keyboard activation must produce exactly one click export per key.
  await client.evaluate(`window.clickExports=0; const original=web.click; web.click=t=>{++clickExports;return original(t)};
    document.activeElement.blur()`);
  for(const event of [{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9},
                     {type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9}])
    await client.send('Input.dispatchKeyEvent',event);
  assert.equal(await client.evaluate('document.activeElement.tagName'),'BUTTON');
  assert.equal(await client.evaluate(`(() => {const el=document.activeElement,s=getComputedStyle(el);
    return el.matches(':focus-visible') && s.outlineStyle!=='none' && parseFloat(s.outlineWidth)>0;})()`),true);
  // Enter activates on keypress, so the keyDown must carry text; Space activates on keyUp.
  for(const [key,code,vk,text] of [[' ','Space',32,' '],['Enter','Enter',13,'\r']]) {
    const before=await client.evaluate('clickExports');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:vk,text});
    await client.send('Input.dispatchKeyEvent',{type:'keyUp',key,code,windowsVirtualKeyCode:vk});
    assert.equal(await client.evaluate('clickExports'),before+1);
  }
  const count=await text();
  await client.evaluate('fixture._fixture_command(1,0)');
  assert.equal(await client.evaluate(`web.click(${button})`),5);
  assert.equal(await text(),count);
  await client.evaluate('fixture._fixture_command(1,1);fixture._fixture_command(0,0)');
  assert.equal(await client.evaluate(`web.click(${button})`),5);
  await client.evaluate('fixture._fixture_command(0,1)');
  assert.equal(await client.evaluate(`getComputedStyle(document.querySelector('[data-loka-token="${button}"]')).display!=='none'`),true);
  assert.equal(await client.evaluate(`web.click(${button})`),0);
  for(const [i,root] of tokens.roots.entries()) {
    const width=250+i*70, height=120+i*30;
    await client.evaluate(`Object.assign(document.querySelector('[data-loka-token="${root}"]').style,
      {width:'${width}px',height:'${height}px'})`);
    for(let attempt=0; attempt<100; ++attempt) {
      const applied=await client.evaluate(`document.querySelector('[data-loka-token="${root}"] span').style.width`);
      if(applied===(width-20)+'px') break;
      await new Promise(resolve=>setTimeout(resolve,20));
    }
    assert.equal(await client.evaluate(`document.querySelector('[data-loka-token="${root}"] span').style.width`),(width-20)+'px');
    assert.equal(await client.evaluate(`web.resize(${root},${width},${height})`),0);
  }
  await client.evaluate(`fixture._fixture_command(2,${tokens.roots[0]})`);
  assert.equal(await client.evaluate(`web.resize(${tokens.roots[0]},100,100)`),5);
  assert.equal(await client.evaluate(`web.click(${button})`),5);
  await client.evaluate('web.quit();web.completed.then(()=>true)');
  assert.equal(await client.evaluate('document.querySelectorAll("[data-loka-token]").length'),0);
  assert.deepEqual(client.errors,[],'browser console/runtime errors');
  for(const roots of [0,1]) {
    const result=await client.evaluate(`(async()=>{
      const m=await LokaWebCounter();m._fixture_configure(${roots},0);
      const started=m.web.start(), count=document.querySelectorAll('body > div[data-loka-token]').length;
      m.web.dispose();await m.web.completed;
      return {started,count,configured:m._fixture_configured(),resources:m.textTest.resources()};
    })()`);
    assert.deepEqual(result,{started:roots ? 0 : 1,count:roots,configured:roots*10,resources:0});
    console.log('fixture configure roots='+roots+': passed');
  }
  const destroy=await client.evaluate(`(async()=>{
    const m=await LokaWebCounter();m._fixture_configure(1,1);m.web.start();
    const old=document.querySelector('button').dataset.lokaToken;
    m._fixture_command(0,0);const removed=!document.querySelector('button');
    m._fixture_command(0,1);const fresh=document.querySelector('button').dataset.lokaToken;
    const configured=m._fixture_configured();m.web.dispose();await m.web.completed;
    return {removed,replaced:old!==fresh,configured,resources:m.textTest.resources()};
  })()`);
  assert.deepEqual(destroy,{removed:true,replaced:true,configured:11,resources:0});
  console.log('fixture configure destroy-on-detach: passed');
  console.log('text cells:', await client.evaluate(`(async()=>{
    document.getElementById('loka-bootstrap-box-probe').remove();
    window.fixture=await LokaWebCounter(); window.web=fixture.web;
    fixture._fixture_configure(1,0);
    if(web.start()!==0) throw new Error('text fixture start');
    await new Promise((resolve,reject)=>{const s=document.createElement('script');
      s.src='text-cells.js';s.onload=resolve;s.onerror=reject;document.head.appendChild(s)});
    const result=await runTextCells(); web.dispose(); await web.completed;
    if(fixture.textTest.service()!==null || fixture.textTest.resources()!==0) throw new Error('service cleanup');
    return result;
  })()`));
  assert.deepEqual(client.errors,[],'text runtime errors');
  const skew=await client.evaluate(`(async()=>{
    await new Promise((resolve,reject)=>{const s=document.createElement('script');
      s.src='skew.js';s.onload=resolve;s.onerror=reject;document.head.appendChild(s)});
    const m=await LokaWebCounter(); const result=m.web.start();
    const mounted=document.querySelectorAll('[data-loka-token]').length;
    await m.web.completed;
    return {result,mounted,resources:m.textTest.resources(),service:m.textTest.service(),ingress:m.web.click(1)};
  })()`);
  assert.deepEqual(skew,{result:2,mounted:0,resources:0,service:null,ingress:4});
  assert.equal(client.errors.length,1,'one refusal diagnostic');
  assert.equal(client.errors[0].params.args[0].value,'[web-text] refused: wire');
  console.log('browser driver: passed (including skewed module refusal)');
} finally {
  if(client) client.socket.close(); child.kill();
  await new Promise(resolve=>server.close(resolve));
  // Chromium still writes its profile for a moment after SIGTERM; a leftover file is not a failure.
  if(child.exitCode===null && child.signalCode===null) await new Promise(resolve=>child.on('exit',resolve));
  await rm(profile,{recursive:true,force:true}).catch(error=>console.warn('profile cleanup:',error.message));
}
