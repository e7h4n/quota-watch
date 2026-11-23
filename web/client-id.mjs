const ascii = new TextEncoder();
export async function readClientId(characteristic) {
  const value = await characteristic.readValue();
  return new TextDecoder().decode(value);
}

export async function writeClientId(characteristic, id) {
  if (id.length > 128 || /[^!-~]/.test(id)) throw Error('Use up to 128 printable characters without spaces.');
  const bytes = ascii.encode(id);
  const write = packet => characteristic.writeValueWithResponse(Uint8Array.from(packet));
  await write([1, bytes.length]);
  for (let offset = 0; offset < bytes.length; offset += 18) {
    await write([2, offset, ...bytes.subarray(offset, offset + 18)]);
  }
  await write([3]);
  if (await readClientId(characteristic) !== id) throw Error('The client ID could not be verified on the watch.');
}
