import assert from 'node:assert/strict';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {readFileSync} from 'node:fs';
const dir=new URL('../../../web/js/',import.meta.url);
const vocab=readFileSync(new URL('style-vocab.js',dir),'utf8');
const expected=JSON.parse(vocab.match(/var LokaWebStyle = (.*);/)[1]).wire;
// Keep the C++ side of the handshake independent of a mutated JS output.
const header=readFileSync(new URL('../../../web/src/WebStyleWire.hpp',import.meta.url),'utf8');
const fingerprint=Number(header.match(/Fingerprint = (0x[\da-f]+)UL/)[1]);
const logs=[];
let rawWidth=7.25, rawAscent=10.2, rawDescent=2.3, lost=false;
const ctx={isContextLost:()=>lost,font:'',fontKerning:'',letterSpacing:'',wordSpacing:'',fontVariantCaps:'',fontStretch:'',
  textRendering:'',direction:'',lang:'',measureText:text=>({width:rawWidth*text.length,fontBoundingBoxAscent:rawAscent,fontBoundingBoxDescent:rawDescent})};
let imports;
const heap=new ArrayBuffer(512);
const context=vm.createContext({TextDecoder,console:{error:s=>logs.push(s)},
  document:{documentElement:{lang:'en'},createElement:()=>({getContext:()=>ctx})},
  navigator:{language:'en'},CSS:{supports:()=>true},
  addToLibrary:lib=>imports=lib, Module:{web:{_complete:()=>{}}},HEAP32:new Int32Array(heap,0,32),HEAPU8:new Uint8Array(heap)});
for(const file of ['style-vocab.js','text.js','library.js']) vm.runInContext(readFileSync(new URL(file,dir),'utf8'),context);
context.WebDOM=imports.$WebDOM;
assert.equal(imports.web_text_admit(fingerprint,expected.count),true,'matching generated sides admit');
assert(context.WebDOM.text);
const style={},el={style:{setProperty:(k,v)=>style[k]=v},setAttribute:()=>{}};
context.WebDOM.resources.set(1,{element:el});
imports.web_set_projected(1,0);
assert.equal(el.style.visibility,'hidden');
imports.web_set_projected(1,1);
assert.equal(el.style.visibility,'visible');
context.HEAP32.set([2,9,1,11,-1,-1,0,-1,0],1);
assert.equal(imports.web_set_text_style(1,4,9),true);
assert.equal(style.font,'10px system-ui');
const before=JSON.stringify(style);
for(const [ptr,count] of [[4,8],[4,10],[3,9],[124,9]]) {
  assert.equal(imports.web_set_text_style(1,ptr,count),false);
  assert.equal(JSON.stringify(style),before);
}
const status=context.LokaWebStyle.webMeasureStatus;
context.HEAPU8.set([97,0,98],160);
const out=100, ascent=104, descent=108;
assert.equal(imports.web_measure(4,160,3,out),status.OK);
assert.equal(context.HEAP32[out/4],22,'exact length includes embedded NUL');
context.HEAPU8.set([0xef,0xbb,0xbf,97],160);
imports.web_set_text(1,160,4);
assert.equal(el.textContent,'\uFEFFa','publication preserves leading U+FEFF');
assert.equal(imports.web_measure(4,160,4,out),status.OK);
assert.equal(context.HEAP32[out/4],15,'standalone range preserves leading U+FEFF');
assert.equal(imports.web_metrics(4,ascent,descent),status.OK);
assert.equal(context.HEAP32[ascent/4],11); assert.equal(context.HEAP32[descent/4],3);
for(const width of [NaN,Infinity,-1,2147483647.1]) {
  rawWidth=width; context.HEAP32[out/4]=123;
  assert.equal(imports.web_measure(4,160,1,out),status.REFUSED);
  assert.equal(context.HEAP32[out/4],123);
}
rawWidth=2147483647;
assert.equal(imports.web_measure(4,160,1,out),status.OK);
assert.equal(context.HEAP32[out/4],2147483647);
lost=true;
context.HEAP32[ascent/4]=123; context.HEAP32[descent/4]=456;
assert.equal(imports.web_metrics(4,ascent,descent),status.REFUSED);
assert.equal(context.HEAP32[ascent/4],123); assert.equal(context.HEAP32[descent/4],456);
assert.equal(imports.web_measure(4,160,1,out),status.REFUSED);
lost=false;
context.HEAP32[1]=1;
assert.equal(imports.web_measure(4,160,1,out),status.PROTOCOL);
assert.equal(imports.web_metrics(4,ascent,descent),status.PROTOCOL);
context.HEAP32[1]=2;
for(const [a,d] of [[-1,2],[0,0],[Infinity,1],[32767,0.1],[NaN,1]]) {
  rawAscent=a; rawDescent=d;
  // Fresh service ensures the metric probe is not a cached successful key.
  context.WebDOM.text=context.LokaWebText.admit(fingerprint,expected.count,
    context.document,context.navigator,context.CSS,()=>{});
  if(context.WebDOM.text) {
    assert.equal(imports.web_metrics(4,ascent,descent),status.REFUSED);
    assert.equal(context.HEAP32[ascent/4],123); assert.equal(context.HEAP32[descent/4],456);
  }
}
rawAscent=10; rawDescent=2;
assert.equal(imports.web_text_admit(fingerprint,expected.count),true);
context.WebDOM.resources.clear();
imports.web_set_projected(1,0);
imports.web_set_projected(1,1);
assert.equal(el.style.visibility,'visible','released token is a no-op');
assert.equal(context.WebDOM.resources.size,0);
assert.equal(imports.web_set_text_style(1,4,9),true,'a released token is a no-op, not a protocol refusal');
imports.web_completed(); assert.equal(context.WebDOM.text,null);
assert.equal(imports.web_text_admit(fingerprint^1,expected.count),false);
assert.equal(context.WebDOM.text,null);
assert.deepEqual(logs,['[web-text] refused: wire']);
console.log('boundary JS: admission, projection refusal, completion release passed (mock DOM; not browser evidence)');
// Deterministic DOM seam: native read-back differs from the requested origin.
const scrollStyle={},contentStyle={};
let sx=0,sy=0,box=true,writes=0;
const scrollElement={style:scrollStyle,getClientRects:()=>box?[{}]:[],
  get scrollLeft(){return sx},set scrollLeft(value){++writes;sx=Math.min(value,9.8)},
  get scrollTop(){return sy},set scrollTop(value){++writes;sy=Math.min(value,19.8)}};
context.WebDOM.resources.set(3,{kind:3,element:scrollElement,content:{style:contentStyle}});
assert.equal(imports.web_commit_scroll(3,5,6,100,80,1,200,300,12,22,100,104),0);
assert.equal(context.HEAP32[25],10);assert.equal(context.HEAP32[26],20);
assert.equal(scrollStyle.left,'5px');assert.equal(contentStyle.height,'300px');
const oldWrites=writes;
assert.equal(imports.web_commit_scroll(3,5,6,100,80,1,200,300,10,20,100,104),0);
assert.equal(writes,oldWrites,'rounded read-back converges without another native assignment');
box=false;
const styles=JSON.stringify([scrollStyle,contentStyle]);
context.HEAP32[25]=123;context.HEAP32[26]=456;
assert.equal(imports.web_commit_scroll(3,0,0,0,0,0,0,0,0,0,100,104),1);
assert.equal(JSON.stringify([scrollStyle,contentStyle]),styles);
assert.equal(context.HEAP32[25],123);assert.equal(context.HEAP32[26],456);
assert.equal(imports.web_commit_scroll(999,0,0,0,0,0,0,0,0,0,100,104),2);
box=true;sy=NaN;
assert.equal(imports.web_commit_scroll(3,0,0,0,0,0,0,0,0,NaN,100,104),2);
assert.equal(context.HEAP32[25],123);assert.equal(context.HEAP32[26],456);
// Exercise the actual creation listener, not a reimplementation of its sampling.
const elements=[]; const events=[];
context.document.createElement=()=>{
  const e={style:{setProperty(){}},dataset:{},children:[],scrollLeft:0,scrollTop:0,
    appendChild(child){this.children.push(child);child.parent=this},
    addEventListener(name,listener,options){this.listener=listener;this.event=name;assert.equal(options.passive,true)},
    removeEventListener(){this.listener=null},remove(){this.removed=true}};
  elements.push(e);return e;
};
context.Module._web_scroll=(...args)=>events.push(args);
const parent={appendChild(){}};
context.WebDOM.resources.set(4,{kind:0,element:parent});
assert.equal(imports.web_create(3,5,4,100,104),1);
const scroller=context.WebDOM.resources.get(5);
assert.equal(context.WebDOM.parent(5),scroller.content);
scroller.element.scrollLeft=2.6;scroller.element.scrollTop=7.2;scroller.listener();
scroller.element.scrollTop=31.7;scroller.listener();
assert.deepEqual(events,[[5,3,7],[5,3,32]],'listener samples current offsets for every delivery');
assert(scroller.content.style.cssText.includes('overflow:clip'));
imports.web_release(5);assert.equal(scroller.element.listener,null);assert.equal(scroller.element.removed,true);
console.log('scroll adapter: applied read-back, BOXLESS no-write, protocol out preservation, live sampling passed');
for(const parentFirst of [true,false]) {
  context.WebDOM.resources.set(40,{kind:0,element:{appendChild(){}}});
  assert.equal(imports.web_create(3,41,40,100,104),1);
  assert.equal(imports.web_create(1,42,41,100,104),1);
  const scrollRow=context.WebDOM.resources.get(41),child=context.WebDOM.resources.get(42);
  const order=parentFirst?[41,42]:[42,41];
  assert.equal(imports.web_release(order[0]),0);
  assert(context.WebDOM.resources.has(order[1]),'other token retains its independent obligation');
  assert.equal(imports.web_release(order[1]),0);
  assert(scrollRow.element.removed && child.element.removed);
  context.WebDOM.resources.delete(40);
}
console.log('scroll adapter: parent-first and child-first release passed');

for (const [name, index] of [[undefined,0],['web',0],['win32',1],['macos',2],['classic',3],
  ['Web',0],['Win32',0],['MACOS',0],['CLASSIC',0],['unknown',0],['constructor',0],['toString',0],['__proto__',0],['',0],
  [1,0],[null,0],[{},0],[['macos'],0],[true,0]]) {
  context.Module.lokaSkin=name;
  assert.equal(imports.web_skin(),index,'skin resolved at pull: '+String(name));
}
console.log('skins: names, fallback and pull-time option passed');

// Build real mapping-skewed schemas using the generator; admission must refuse
// before canvas/service or resource creation, even though the text fields agree.
for (const mutation of ['reorder','rename']) {
  const generated=spawnSync('python3',['-c',`
import importlib.util, json
from pathlib import Path
root=Path.cwd()
spec=importlib.util.spec_from_file_location('g',root/'tools/ci/gen_style_vocab.py')
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
v=json.loads((root/'tools/style-vocab.json').read_text())
if '${mutation}' == 'reorder': v['webSkins'][1:3]=reversed(v['webSkins'][1:3])
else: v['webSkins'][1]['name']='renamed'
print(g.generate_web(v))
`],{cwd:fileURLToPath(new URL('../../../',import.meta.url)),encoding:'utf8'});
  assert.equal(generated.status,0,generated.stderr);
  vm.runInContext(generated.stdout,context);
  context.WebDOM.resources.clear();context.WebDOM.text=null;
  assert.equal(imports.web_text_admit(fingerprint,expected.count),false,'skin '+mutation+' skew refuses');
  assert.equal(context.WebDOM.text,null);
  assert.equal(context.WebDOM.resources.size,0);
}
console.log('skins: generated reorder/rename skew refused before resources');
