import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const context = vm.createContext({console});
for (const file of ['style-vocab.js','text.js']) vm.runInContext(readFileSync(new URL('../../../web/js/'+file, import.meta.url),'utf8'), context);
const style=context.LokaWebStyle, text=context.LokaWebText;
const words=[2,9,0,0,-1,-1,0,-1,0];
const heap=new Int32Array(20); heap.set(words,1);
const decode=(ptr=4,count=9)=>style.decodeTextDescriptor(heap,ptr,count);
assert(Object.isFrozen(decode()));
for (const count of [8,10,9.5,NaN]) assert.equal(decode(4,count),null);
for (const ptr of [-4,2,heap.byteLength-32,2**32,NaN,Infinity,4.5]) assert.equal(decode(ptr),null);
for (const [field,value] of [[0,1],[1,10],[2,3],[3,1],[4,2],[5,-2],[6,3],[7,2],[8,11]]) {
  heap.set(words,1); heap[1+field]=value; assert.equal(decode(),null);
}
heap.set(words,1); heap[3]=2; heap[4]=32767; assert.equal(decode(),null);
for (const points of [-32768,32767]) { heap[3]=1; heap[4]=points; assert.equal(decode().sizePayload,points); }
for (const points of [-32769,32768]) { heap[4]=points; assert.equal(decode(),null); }
const d={sizeKind:1,sizePayload:12,weight:-1,italic:-1,family:0,color:0,underline:-1};
for (const [i,points] of [-32768,0,9,11,16,21,24,32767].entries())
  assert.equal(style.sizes[Math.floor(style.fontKey({...d,sizePayload:points})/8)],[9,9,9,10,14,18,24,24][i]);
assert.equal(style.fontKey({...d,sizeKind:0}),16);
assert.equal(style.fontKey({...d,sizeKind:2,sizePayload:0}),8);
assert.equal(style.fontKey({...d,sizePayload:10}),8);
assert.equal(style.fontKey({...d,color:10,underline:1}),16);
assert.equal(style.fontKey({...d,weight:1,italic:1,family:2}),23);
const canvasDefaults={font:'10px sans-serif',fontKerning:'auto',letterSpacing:'0px',wordSpacing:'0px',
  fontVariantCaps:'normal',fontStretch:'normal',textRendering:'auto',direction:'inherit',lang:'inherit'};
let lastCanvas;
const document={documentElement:{lang:'ja'},createElement:()=>lastCanvas={getContext:()=>({...canvasDefaults,
  measureText:()=>({fontBoundingBoxAscent:10,fontBoundingBoxDescent:3})})}};
const logs=[];
const admit=(doc=document,css={supports:()=>true},fingerprint=style.wire.fingerprint)=>
  text.admit(fingerprint,9,doc,{language:'en'},css,s=>logs.push(s));
const service=admit(); assert(service); assert.equal(service.canvas,lastCanvas);
const env=service.environment(16); assert(Object.isFrozen(env)); assert.equal(env,service.environment(16));
const canvas={}, css={}, attrs={};
env.applyToCanvas(canvas); env.applyToElement({style:{setProperty:(k,v)=>css[k]=v},setAttribute:(k,v)=>attrs[k]=v});
for (const [k,v] of Object.entries(canvas)) {
  if (k==='lang') assert.equal(attrs.lang,v);
  else assert.equal(css[k.replace(/[A-Z]/g,c=>'-'+c.toLowerCase())],v,k);
}
assert.equal(css.font,'12px system-ui'); assert(!css.font.includes('/')); assert.equal(css['white-space'],'pre');
for (const [key,value] of Object.entries({'font-synthesis':'initial','font-size-adjust':'none',
  'font-feature-settings':'normal','font-variation-settings':'normal','font-optical-sizing':'auto',
  'text-transform':'none','writing-mode':'horizontal-tb','text-orientation':'mixed','text-indent':'0',
  'text-spacing-trim':'normal','text-autospace':'normal','text-size-adjust':'none',
  '-webkit-text-size-adjust':'none','font-palette':'normal','unicode-bidi':'normal'})) assert.equal(css[key],value);
document.documentElement.lang='tr'; service.environment(0).applyToCanvas(canvas); assert.equal(canvas.lang,'ja');
assert.equal(admit(document,undefined,0),null); assert.equal(logs.at(-1),'[web-text] refused: wire');
for (const prop of Object.keys(canvasDefaults)) {
  const ctx={...canvasDefaults,measureText:()=>({fontBoundingBoxAscent:10,fontBoundingBoxDescent:3})}; delete ctx[prop];
  assert.equal(admit({documentElement:{lang:''},createElement:()=>({getContext:()=>ctx})}),null,prop);
}
assert.equal(admit(document,null),null);
assert.equal(admit(document,{supports:()=>false}),null);
document.documentElement.lang='inherit'; assert.equal(admit().lang,'en');
console.log('text JS: decoder, fontKey, Env parity, admission and fixed language passed');
// Every one of the 48 keys has one stable Env; malformed keys cannot grow the Map.
const all=admit();
const identities=new Set();
for(let key=0;key<48;++key) { identities.add(all.environment(key)); assert.equal(all.environment(key),all.environment(key)); }
assert.equal(identities.size,48);
for(const key of [-1,48,1.5,NaN]) assert.throws(()=>all.environment(key));
const inert={...canvasDefaults};
Object.defineProperty(inert,'letterSpacing',{get:()=> '0px',set:()=>{}});
inert.measureText=()=>({fontBoundingBoxAscent:10,fontBoundingBoxDescent:3});
assert.equal(admit({documentElement:{lang:'en'},createElement:()=>({getContext:()=>inert})}),null);
const nan={...canvasDefaults,measureText:()=>({fontBoundingBoxAscent:NaN,fontBoundingBoxDescent:3})};
assert.equal(admit({documentElement:{lang:'en'},createElement:()=>({getContext:()=>nan})}),null);
const declarations=[];
assert(admit(document,{supports:(name)=>{declarations.push(name); return name!=='text-size-adjust';}}));
assert(declarations.includes('-webkit-text-size-adjust'));
assert(!declarations.includes('lang'));
assert(declarations.every(name=>name===name.toLowerCase()));
