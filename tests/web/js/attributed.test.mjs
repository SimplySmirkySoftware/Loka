import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const dir=new URL('../../../web/js/',import.meta.url);
let imports, fail='', childIndex=0;
function element() {
  return {dataset:{},remove(){},style:{setProperty(k,v){this[k]=v}},setAttribute(){},children:[],
    appendChild(c){this.children.push(c)},replaceChildren(f){this.children=f.children}};
}
const document={createElement(){if(fail==='child'+childIndex++) throw Error('allocation');return element()},
  createDocumentFragment(){if(fail==='fragment') throw Error('allocation');return element()}};
const heap=new ArrayBuffer(4096);
const context=vm.createContext({TextDecoder,document,HEAP32:new Int32Array(heap),HEAPU8:new Uint8Array(heap),
  addToLibrary:lib=>imports=lib});
for(const f of ['style-vocab.js','text.js','library.js']) vm.runInContext(readFileSync(new URL(f,dir),'utf8'),context);
context.WebDOM=imports.$WebDOM;
const service={environment(){if(fail==='env') throw Error('allocation');return {applyToElement(el){
  if(fail==='apply') throw Error('allocation');el.style.font='12px system-ui';el.style.lineHeight='normal';}}}};
context.WebDOM.text=service;
const row={kind:7,element:element()};context.WebDOM.resources.set(1,row);
const desc=[2,9,1,12,0,0,1,0,5];
const words=[1,2,2, 0,0,0,8,13,0,1, 1,8,0,8,13,1,2, ...desc,...desc.slice(0,7),1,7];
const raw=[97,98];
function set(w=words,b=raw){context.HEAP32.set(w,1);context.HEAPU8.set(b,2048);return [1,4,w.length,2048,b.length]}
function apply(w=words,b=raw){childIndex=0;return imports.web_set_runs(...set(w,b))}
assert.equal(apply(),0);
assert.deepEqual(row.element.children.map(c=>c.textContent),['a','b']);
assert.equal(row.element.children[0].style.color,'red');
assert.equal(row.element.children[1].style.color,'blue');
assert.equal(row.element.children[1].style.textDecoration,'underline');
assert.equal(row.element.children[1].style.lineHeight,'13px');
assert.equal(row.element.children[1].style.left,'8px');
const children=row.element.children.slice(), payload=row.runs;
assert.equal(apply(),0);assert(children.every((c,i)=>c===row.element.children[i]));
function protocol(w,b=raw,args) {
  const old=row.runs, kids=row.element.children.slice();
  assert.equal(args?imports.web_set_runs(...args):apply(w,b),2);
  assert.equal(row.runs,old);assert(kids.every((c,i)=>c===row.element.children[i]));
}
for(const [index,value] of [[0,2],[1,-1],[1,2147483647],[2,-1],[3,-1],[3,2],[4,-1],[5,-1],
  [6,-1],[7,0],[4,32767],[5,32767],[8,-1],[9,3],[15,0],[16,3],[words.length-1,999]]) {
  const w=words.slice();w[index]=value;protocol(w);
}
for(const args of [[1,3,words.length,2048,2],[1,4,2,2048,2],[1,4092,3,2048,2],
  [1,4,3.5,2048,2],[1,4,words.length,2048,0.5],[1,4,-1,2048,2],[1,4,2147483648,2048,2],[1,4,words.length,2048,-1],
  [1,4,words.length,2048,2147483648],[1,-2147483648,3,0,0],
  [1,4,words.length,4096,2]]) {set();protocol(null,raw,args)}
protocol([1,0,0],[97]);
for(const b of [[10,98],[13,98],[0xc0,0x80],[0xed,0xa0,0x80],[0xf4,0x90,0x80,0x80],
  [0xe3,0x81],[0x80],[0xf0,0x80,0x80,0x80]]) {
  const w=[1,1,1,0,0,0,8,13,0,b.length,...desc];protocol(w,b);
}
// A valid scalar split across fragments is still an invalid range boundary.
protocol([1,2,1,0,0,0,8,13,0,1,0,8,0,8,13,1,3,...desc],[0xe3,0x81,0x82]);
for(const b of [[0xef,0xbb,0xbf,97],[0xf0,0x9f,0x98,0x80],[0,9,12]]) {
  const w=[1,1,1,0,0,0,0,13,0,b.length,...desc];
  assert.equal(apply(w,b),0);assert.equal(row.element.children[0].textContent,new TextDecoder('utf-8',{ignoreBOM:true}).decode(new Uint8Array(b)));
  assert.equal(row.element.children[0].style.overflow,'visible');
}
// Accepted structural superset: unused descriptor, empty range, unsnapped size.
const unsnapped=desc.slice();unsnapped[3]=11;
assert.equal(apply([1,1,2,0,0,0,0,13,0,0,...unsnapped,...desc],[]),0);
assert.equal(apply([1,0,0],[]),0);assert.equal(row.element.children.length,0);
set([1,0,0],[]);assert.equal(imports.web_set_runs(1,4,3,0,0),0);
for(const stage of ['fragment','child0','child1','env','apply','descriptor','copyWords','copyBytes','decoder']) {
  fail='';assert.equal(apply(),0);
  const old=row.element.children.slice();
  const decode=context.LokaWebStyle.decodeTextDescriptor;
  if(stage==='descriptor') context.LokaWebStyle.decodeTextDescriptor=()=>{throw Error('allocation')};
  if(stage==='copyWords') { let allocations=0; context.Int32Array=class {constructor(...args){
    if(allocations++===2) throw Error('copy allocation');return new Int32Array(...args);}}; }
  if(stage==='copyBytes') context.Uint8Array=class {constructor(){throw Error('allocation')}};
  if(stage==='decoder') context.TextDecoder=class {constructor(){throw Error('allocation')}};
  fail=stage;
  const changed=words.slice();changed[4]=1;
  assert.equal(apply(stage==='descriptor'?words:changed),1,stage);
  assert.equal(row.runs,undefined,stage);assert(old.every((c,i)=>row.element.children[i]===c),stage);
  context.LokaWebStyle.decodeTextDescriptor=decode;
  delete context.Int32Array;delete context.Uint8Array;context.TextDecoder=TextDecoder;fail='';
  assert.equal(apply(),0,stage+' recovery');
}
set();assert.equal(imports.web_set_runs(999,4,words.length,2048,2),0);
set([0,0,0],[]);assert.equal(imports.web_set_runs(999,4,3,0,0),2);
set();row.kind=1;protocol(words);row.kind=7;
context.WebDOM.text=null;protocol(words);
assert.equal(imports.web_set_runs(999,4,words.length,2048,2),2);
context.WebDOM.text=service;
assert.equal(apply(),0);
assert.notEqual(row.runs.words.buffer,heap);assert.notEqual(row.runs.bytes.buffer,heap);
console.log('attributed JS: validation, staging refusal, recovery, paint and child identity passed');

// Signed i32 addresses are unsigned Wasm32 bits, without allocating a 2GB heap.
const old32=context.HEAP32, old8=context.HEAPU8;
context.HEAPU8={length:2147483660};
context.HEAP32={536870912:1,536870913:0,536870914:0};
assert.equal(imports.web_set_runs(999,-2147483648,3,0,0),0);
context.HEAP32=old32;context.HEAPU8=old8;
assert.equal(apply([1,0,1,...desc],[]),0);
assert.equal(apply(),0);
const beforeDecode=row.runs, beforeChildren=row.element.children.slice();
vm.runInContext("TextDecoder=class {decode(){throw new TypeError('fatal UTF-8')}}",context);
const changed=words.slice();changed[4]=1;
assert.equal(apply(changed),2);
assert.equal(row.runs,beforeDecode);assert(beforeChildren.every((c,i)=>c===row.element.children[i]));
context.TextDecoder=TextDecoder;
assert.equal(imports.web_create(7,2,1,0,0),1);
const created=context.WebDOM.row(2);
assert.equal(created.listener,null);assert.equal(created.element.style.margin,'0');
assert.equal(created.element.style.padding,'0');assert.equal(created.element.style.border,'0');
assert.equal(created.element.style.overflow,'hidden');
imports.web_release(2);assert.equal(context.WebDOM.row(2),undefined);
console.log('attributed JS: unsigned high-bit address, fatal decode, resource creation and release passed');

// Named paint mapping follows generated enum values rather than array positions.
const colors=context.LokaWebStyle.colors;
[colors.RED,colors.BLUE]=[colors.BLUE,colors.RED];
const painted=context.WebDOM.fragmentBox([0,0,0,8,13],0,
  {sizeKind:1,sizePayload:12,weight:0,italic:0,family:1,color:colors.RED,underline:0},'x',service);
assert.equal(painted.style.color,'red');
[colors.RED,colors.BLUE]=[colors.BLUE,colors.RED];
