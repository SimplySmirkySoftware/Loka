#!/usr/bin/env node
import {spawn, spawnSync} from 'node:child_process';
import {createServer} from 'node:http';
import {readFile, mkdtemp, rm} from 'node:fs/promises';
import {resolve, join, extname, basename} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const driverDir=fileURLToPath(new URL('.',import.meta.url));
import assert from 'node:assert/strict';

const build=resolve(process.argv[2] || 'build/wasm/testing/web');
const chromium=process.env.CHROMIUM || ['chromium','chromium-browser','google-chrome']
  .find(name=>spawnSync('which',[name],{stdio:'ignore'}).status===0);
if(!chromium) { console.log('[skip] browser driver: no chromium'); process.exit(0); }
if(typeof WebSocket==='undefined') throw new Error('This Node has no built-in WebSocket client; use the emsdk Node.');
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
          || (message.method==='Runtime.consoleAPICalled' && message.params.type==='error'))
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
const profile=await mkdtemp(join(tmpdir(),'loka-web-chrome-'));
const server=createServer(async(req,res)=>{
  try {
    const file=basename(new URL(req.url,'http://localhost').pathname) || 'index.html';
    let data;
    if(file === 'text-cells.js') data=await readFile(join(driverDir,file));
    else if(file === 'skew.js' || file === 'LokaWebCounter.js') {
      data=await readFile(join(build,'LokaWebCounter.js'),'utf8');
      // Driver-only closure access, never a production export or a C++ test-shaped door.
      const hook=`Module['textTest']={
        service:()=>WebDOM.text,
        replaceService:service=>{WebDOM.text=service},
        resources:()=>WebDOM.resources.size,
        admit:()=>LokaWebText.admit(LokaWebStyle.wire.fingerprint,LokaWebStyle.wire.count,
          document,navigator,CSS,message=>console.error(message)),
        badDescriptor:token=>_web_set_text_style(token,0,8)
      };
`;
      assert(data.includes('// Captures this module forever;'));
      data=data.replace('// Captures this module forever;',hook+'// Captures this module forever;');
      if(file === 'skew.js') data=data.replace(/"fingerprint":\d+/, '"fingerprint":0');
    } else data=await readFile(join(build,file));
    const type={'.html':'text/html','.js':'application/javascript','.wasm':'application/wasm'}[extname(file)];
    res.writeHead(200,{'Content-Type':type || 'application/octet-stream'});res.end(data);
  } catch {res.writeHead(404);res.end();}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
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
  await client.send('Page.navigate',{url:'http://127.0.0.1:'+server.address().port+'/index.html'});
  for(let i=0; i<100 && !await client.evaluate('!!window.ready'); ++i)
    await new Promise(resolve=>setTimeout(resolve,50));
  assert.equal(await client.evaluate('!!window.ready'),true,'module ready');
  // Inspect bootstrap synchronously, before ResizeObserver can repair a wrong box.
  const initial=await client.evaluate(`(() => {
    const style=document.createElement('style');
    style.id='loka-bootstrap-box-probe';
    style.textContent='body > div[data-loka-token] { padding:7px; border:3px solid; transform:scale(.9); }';
    document.head.appendChild(style);
    const result=web.start(2);
    return {result,widths:[...document.querySelectorAll('body > div span')].map(e=>e.style.width)};
  })()`);
  assert.equal(initial.result,0);
  assert.deepEqual(initial.widths,['300px','300px']);
  const tokens=await client.evaluate(`({buttons:[...document.querySelectorAll('button')].map(e=>+e.dataset.lokaToken),
    roots:[...document.querySelectorAll('body > div')].map(e=>+e.dataset.lokaToken)})`);
  const button=tokens.buttons[0];
  const text=()=>client.evaluate(`document.querySelector('[data-loka-token="${tokens.roots[0]}"] span').textContent`);
  await client.evaluate(`document.querySelector('[data-loka-token="${button}"]').click()`);
  assert.equal(await text(),'Count: 1');
  // Native keyboard activation must produce exactly one click export per key.
  await client.evaluate(`window.clickExports=0; const original=web.click; web.click=t=>{++clickExports;return original(t)};
    document.activeElement.blur()`);
  for(const event of [{type:'keyDown',key:'Tab',code:'Tab',windowsVirtualKeyCode:9},
                     {type:'keyUp',key:'Tab',code:'Tab',windowsVirtualKeyCode:9}])
    await client.send('Input.dispatchKeyEvent',event);
  assert.equal(await client.evaluate('document.activeElement.tagName'),'BUTTON');
  assert.equal(await client.evaluate(`(() => {const el=document.activeElement,s=getComputedStyle(el);
    return el.matches(':focus-visible') && s.outlineStyle!=='none' && parseFloat(s.outlineWidth)>0;})()`),true);
  for(const [key,code,vk] of [[' ','Space',32],['Enter','Enter',13]]) {
    const before=await client.evaluate('clickExports');
    await client.send('Input.dispatchKeyEvent',{type:'keyDown',key,code,windowsVirtualKeyCode:vk});
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
  console.log('text cells:', await client.evaluate(`(async()=>{
    document.getElementById('loka-bootstrap-box-probe').remove();
    window.fixture=await LokaWebCounter(); window.web=fixture.web;
    if(web.start(1)!==0) throw new Error('text fixture start');
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
    const m=await LokaWebCounter(); const result=m.web.start(1);
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
  await rm(profile,{recursive:true,force:true});
}
