import {test} from 'node:test';
import assert from 'node:assert/strict';
import {encode, decode, save, list} from '../web/protocol.mjs';

const card={version:1, id:'aa'.repeat(16), email:'alex@example.org', provider:'ChatGPT', tier:'Pro',
  captured:1800000000,short:{remaining:0,reset:1800001800},week:{remaining:null,reset:null},creditCents:1800,resets:0};

test('wire offsets match quota_card and unknown is distinct from zero',()=>{
  const bytes=encode(card);
  assert.equal(bytes.length,184);
  assert.equal(bytes[21],0); assert.equal(bytes[22],255);
  assert.equal(bytes[57],97); assert.equal(bytes[154],80);
  assert.deepEqual(decode(bytes),card);
  assert.throws(()=>encode({...card,short:{remaining:101}}));
  assert.throws(()=>encode({...card,email:'a'.repeat(97)}));
});
test('fragments fit minimum ATT MTU and round trip through a state read',async()=>{
  const chunks=[];
  await save(async p=>chunks.push(p),card);
  assert.deepEqual(chunks.shift(),[1]); assert.deepEqual(chunks.pop(),[3]);
  const bytes=[];
  for(const [command,offset,...body] of chunks) {
    assert.equal(command,2); assert.equal(offset,bytes.length); assert.ok(body.length<=18);
    bytes.push(...body);
  }
  const result=await list(async p=>assert.deepEqual(p,[4,0]),async()=>new DataView(Uint8Array.from([1,1,...bytes]).buffer));
  assert.deepEqual(result,[card]);
});
