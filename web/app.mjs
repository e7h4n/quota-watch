import {uuid,encode,list,save} from './protocol.mjs';
const el=id=>document.getElementById(id);
let device,command,state,card,busy=false;
function update() {
  el('connection').textContent=state?'已连接':'未连接';
  el('connect').textContent=state?'断开连接':'连接手表';
  el('connect').disabled=busy || !navigator.bluetooth;
  el('reload').disabled=busy || !state;
  el('save').disabled=busy || !state || !card;
  el('file').disabled=busy;
  document.querySelectorAll('.account button').forEach(b=>b.disabled=busy || !state);
}
async function run(action) {
  if(busy) return;
  busy=true; el('status').textContent=''; update();
  try {await action();} catch(error) {el('status').textContent=error.message;}
  finally {busy=false; update();}
}
function disconnected() {
  command=state=null;
  el('accounts').replaceChildren();
  update();
}
const write=bytes=>command.writeValueWithResponse(Uint8Array.from(bytes));
async function refresh() {
  const cards=await list(write,()=>state.readValue());
  el('accounts').replaceChildren();
  for(const account of cards) {
    const row=document.createElement('div'); row.className='account';
    const title=document.createElement('strong'), sub=document.createElement('small'), remove=document.createElement('button');
    title.textContent=account.email;
    sub.textContent=[account.provider,account.tier,account.resets==null?'':'可用 Reset '+account.resets+' 次',
      new Date(account.captured*1000).toLocaleString()].filter(Boolean).join(' · ');
    remove.textContent='移除';
    remove.onclick=()=>run(async()=>{
      if(!confirm('从手表移除 '+account.email+'？')) return;
      await write([5,...account.id.match(/../g).map(s=>parseInt(s,16))]); await refresh();
    });
    row.append(title,sub,remove); el('accounts').append(row);
  }
  if(!cards.length) el('accounts').textContent='手表上还没有账号';
  return cards;
}
el('connect').onclick=()=>run(async()=>{
  if(state) {device.gatt.disconnect(); disconnected();return;}
  device=await navigator.bluetooth.requestDevice({filters:[{services:[uuid(1)]}]});
  device.addEventListener('gattserverdisconnected',disconnected,{once:true});
  try {
    const server=await device.gatt.connect(), service=await server.getPrimaryService(uuid(1));
    command=await service.getCharacteristic(uuid(2)); state=await service.getCharacteristic(uuid(3));
    const clock=new Uint8Array(5);clock[0]=6;new DataView(clock.buffer).setUint32(1,Math.floor(Date.now()/1000),true);
    await write(clock); await refresh();
  } catch(error) {device.gatt.disconnect();disconnected();throw error;}
});
el('reload').onclick=()=>run(refresh);
el('file').onchange=()=>run(async()=>{
  card=null;el('preview').textContent='';
  const file=el('file').files[0];
  if(!file) return;
  if(file.size>8192) throw Error('请选择导出的 Quota Watch 账号卡');
  const input=JSON.parse(await file.text()); encode(input); card=input;
  el('preview').textContent=[card.email,card.provider,card.tier].join(' · ');
});
el('save').onclick=()=>run(async()=>{
  await save(write,card);
  const accounts=await refresh();
  const stored=accounts.find(c=>c.id===card.id);
  const expected=encode(card);
  if(!stored || !encode(stored).every((b,i)=>b===expected[i])) throw Error('写入后的读回校验失败');
  el('status').textContent='已保存并核对 '+card.email;
});
if(!navigator.bluetooth) el('status').textContent='请在支持 Web Bluetooth 的桌面 Chrome 或 Edge 中打开。';
update();
