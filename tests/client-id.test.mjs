import {test} from 'node:test';
import assert from 'node:assert/strict';
import {writeClientId} from '../web/client-id.mjs';

function recorder(readback) {
  const packets = [];
  return {
    packets,
    writeValueWithResponse: async value => packets.push([...value]),
    readValue: async () => new DataView(new TextEncoder().encode(readback).buffer)
  };
}
test('client IDs are fragmented at ATT minimum MTU and verified',async () => {
  const id = 'case-Sensitive_9'.repeat(8), chr = recorder(id);
  await writeClientId(chr,id);
  assert.deepEqual(chr.packets[0],[1,128]);
  assert.deepEqual(chr.packets.at(-1),[3]);
  const bytes = [];
  for (const [op, offset, ...chunk] of chr.packets.slice(1,-1)) {
    assert.equal(op,2);
    assert.equal(offset,bytes.length);
    assert.ok(chunk.length <= 18);
    bytes.push(...chunk);
  }
  assert.equal(new TextDecoder().decode(Uint8Array.from(bytes)),id);
});
test('clearing and bad readback',async () => {
  const chr = recorder('');
  await writeClientId(chr,'');
  assert.deepEqual(chr.packets,[[1,0],[3]]);
  await assert.rejects(writeClientId(chr,'other'),/verified/);
});
test('reject invalid input before any write',async () => {
  const chr = recorder('');
  for (const id of ['space here','x'.repeat(129),'α','\n']) await assert.rejects(writeClientId(chr,id));
  assert.equal(chr.packets.length,0);
});
