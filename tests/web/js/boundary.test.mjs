import assert from 'node:assert/strict';
import vm from 'node:vm';
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
assert.equal(imports.web_set_text_style(1,4,9),true,'a released token is a no-op, not a protocol refusal');
imports.web_completed(); assert.equal(context.WebDOM.text,null);
assert.equal(imports.web_text_admit(fingerprint^1,expected.count),false);
assert.equal(context.WebDOM.text,null);
assert.deepEqual(logs,['[web-text] refused: wire']);
console.log('boundary JS: admission, projection refusal, completion release passed (mock DOM; not browser evidence)');
