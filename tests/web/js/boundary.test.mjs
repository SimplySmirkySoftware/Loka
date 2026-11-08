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
const ctx={font:'',fontKerning:'',letterSpacing:'',wordSpacing:'',fontVariantCaps:'',fontStretch:'',
  textRendering:'',direction:'',lang:'',measureText:()=>({fontBoundingBoxAscent:10,fontBoundingBoxDescent:2})};
let imports;
const context=vm.createContext({console:{error:s=>logs.push(s)},
  document:{documentElement:{lang:'en'},createElement:()=>({getContext:()=>ctx})},
  navigator:{language:'en'},CSS:{supports:()=>true},
  addToLibrary:lib=>imports=lib, Module:{web:{_complete:()=>{}}},HEAP32:new Int32Array(32)});
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
context.WebDOM.resources.clear(); imports.web_completed(); assert.equal(context.WebDOM.text,null);
assert.equal(imports.web_text_admit(fingerprint^1,expected.count),false);
assert.equal(context.WebDOM.text,null);
assert.deepEqual(logs,['[web-text] refused: wire']);
console.log('boundary JS: admission, projection refusal, completion release passed (mock DOM; not browser evidence)');
