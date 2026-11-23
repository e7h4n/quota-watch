export const uuid = n => '0000000' + n + '-3e18-4c37-a4fb-81ad2d438a51';
const encoder = new TextEncoder(), decoder = new TextDecoder();

function integer(x, min, max, name) {
  if (!Number.isInteger(x) || x < min || x > max) throw Error('Invalid field: ' + name);
  return x;
}
export function encode(card) {
  if (card.version !== 1 || !['ChatGPT','Claude'].includes(card.provider) ||
      !/^[a-f0-9]{32}$/.test(card.id)) throw Error('Choose an exported Quota Watch account card.');
  const bytes = new Uint8Array(184), view = new DataView(bytes.buffer);
  function string(offset, capacity, s) {
    if (typeof s !== 'string' || !s.trim() || /[\x00-\x1f\x7f]/.test(s)) throw Error('The account card is missing valid account information.');
    const value = encoder.encode(s);
    if (value.length >= capacity) throw Error('An account field is too long.');
    bytes.set(value,offset);
  }
  view.setUint32(0,integer(card.captured,1600000000,0xffffffff,'captured'),true);
  for (const [i, window] of [card.short,card.week].entries()) {
    const reset = window?.reset;
    view.setUint32(4+4*i,integer(reset ?? 0,0,0xffffffff,'reset'),true);
    bytes[21+i] = window?.remaining == null ? 255 : integer(window.remaining,0,100,'remaining');
  }
  view.setInt32(12,integer(card.creditCents ?? -1,-1,2147483647,'creditCents'),true);
  view.setInt32(16,integer(card.resets ?? -1,-1,2147483647,'resets'),true);
  bytes[20] = Number(card.provider === 'Claude');
  string(24,33,card.id); string(57,97,card.email); string(154,30,card.tier);
  return bytes;
}
export function decode(bytes) {
  if (bytes.length !== 184) throw Error('Unexpected account card length.');
  const v = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  function string(offset,length) {
    const field = bytes.subarray(offset,offset+length), end = field.indexOf(0);
    if (end < 0) throw Error('Invalid text in the account card.');
    return decoder.decode(field.subarray(0,end));
  }
  const value = n => n < 0 ? null : n;
  const window = i => ({reset:v.getUint32(4+4*i,true) || null, remaining:bytes[21+i]===255?null:bytes[21+i]});
  return {version:1, id:string(24,33), email:string(57,97), tier:string(154,30),
    captured:v.getUint32(0,true), provider:bytes[20]?'Claude':'ChatGPT',short:window(0),week:window(1),
    resets:value(v.getInt32(16,true)),creditCents:value(v.getInt32(12,true))};
}
export async function save(write,card) {
  const bytes=encode(card);
  await write([1]);
  for(let offset=0;offset<bytes.length;offset+=18)
    await write([2,offset,...bytes.subarray(offset,offset+18)]);
  await write([3]);
}
export async function list(write,read) {
  const accounts=[];
  for(let i=0;i<8;i++) {
    await write([4,i]);
    const v=await read();
    if(v.byteLength<2 || v.getUint8(0)!==1 || v.getUint8(1)>8) throw Error('The watch returned an incompatible protocol version or account count.');
    if(i>=v.getUint8(1)) break;
    accounts.push(decode(new Uint8Array(v.buffer,v.byteOffset+2,v.byteLength-2)));
    if(accounts.length===v.getUint8(1)) break;
  }
  return accounts;
}
