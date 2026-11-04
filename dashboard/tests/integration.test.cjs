const test=require('node:test'),assert=require('node:assert/strict'),net=require('node:net'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {spawn}=require('node:child_process');
const mqtt=require('mqtt');
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function port(){const s=net.createServer();await new Promise(r=>s.listen(0,'127.0.0.1',r));const p=s.address().port;await new Promise(r=>s.close(r));return p;}
async function until(fn,timeout=8000){const at=Date.now();while(Date.now()-at<timeout){const value=await fn();if(value)return value;await delay(80);}throw new Error('Timed out');}
async function fixture(t,demo){
  const httpPort=await port(),mqttPort=await port(),dir=fs.mkdtempSync(path.join(os.tmpdir(),'urex-system-'));
  const child=spawn(process.execPath,['server.cjs',...(demo?['--demo']:[])],{cwd:path.resolve(__dirname,'..'),env:{...process.env,
    HTTP_HOST:'127.0.0.1',HTTP_PORT:String(httpPort),MQTT_HOST:'127.0.0.1',MQTT_PORT:String(mqttPort),UREX_DATA_DIR:dir,
    MQTT_USER:'urex-gateway',MQTT_PASSWORD:'test-gateway-secret',DASHBOARD_USER:'operator',DASHBOARD_PASSWORD:'test-dashboard-secret'},stdio:['ignore','pipe','pipe']});
  let output='';child.stdout.on('data',d=>output+=d);child.stderr.on('data',d=>output+=d);
  t.after(async()=>{child.kill();await Promise.race([new Promise(r=>child.once('exit',r)),delay(2000)]);});
  const url=`http://127.0.0.1:${httpPort}`;
  const authorization='Basic '+Buffer.from('operator:test-dashboard-secret').toString('base64');
  async function state(){const res=await fetch(url+'/api/state',{headers:demo?{}:{authorization}});return res.json();}
  await until(async()=>{try{return await state();}catch{return false;}});
  async function post(route,body){const s=await state();return fetch(url+route,{method:'POST',headers:{...(demo?{}:{authorization}),'Content-Type':'application/json','X-Urex-Token':s.control_token},body:JSON.stringify(body)});}
  return{url,mqttPort,state,post,output:()=>output};
}
test('MQTT demo: acknowledge arm, persist hit, deduplicate, expire control lease',async t=>{
  const f=await fixture(t,true);
  await until(async()=>{const s=await f.state();return s.controller_online&&s.gateway;});
  await f.post('/api/heartbeat',{});await until(async()=>(await f.state()).telemetry.lease);
  assert.equal((await f.post('/api/command',{action:'target',blade:3})).status,202);
  await until(async()=>(await f.state()).last_command?.result==='applied');
  assert.equal((await f.post('/api/demo-hit',{blade:3,ring:10})).status,200);
  let s=await until(async()=>{const s=await f.state();return s.hit_count===1&&s;});
  assert.equal(s.score,10);assert.equal(s.events[0].blade_id,3);
  await delay(900);s=await f.state();assert.equal(s.hit_count,1);assert.equal(s.telemetry.buffered_events,0);
  await until(async()=>!(await f.state()).telemetry.bench,7000);
  assert.equal((await f.state()).telemetry.reason,'control_link_lost');
  const invalid=await f.post('/api/command',{action:'start',rpm:99});assert.equal(invalid.status,400);
  const nullBody=await f.post('/api/command',null);assert.equal(nullBody.status,400);
});
test('live server: authentication, no retained commands, device ACL, hit replay',async t=>{
  const f=await fixture(t,false);
  assert.equal((await fetch(f.url+'/api/state')).status,401);
  assert.equal((await fetch(f.url+'/api/command',{method:'POST',body:'{}',headers:{authorization:'Basic '+Buffer.from('operator:test-dashboard-secret').toString('base64')}})).status,403);
  const client=mqtt.connect(`mqtt://127.0.0.1:${f.mqttPort}`,{username:'urex-gateway',password:'test-gateway-secret',clientId:'integration-device',reconnectPeriod:0});
  t.after(()=>client.end(true));client.on('error',()=>{});
  await new Promise(r=>client.once('connect',r));
  client.publish('urex/rune/gateway','{"online":true}',{retain:true});
  const e={kind:'hit',boot_id:55,event_id:1,session_id:'55-1',blade_id:2,ring:7,time_ms:123};
  const telemetry={kind:'telemetry',boot_id:55,session_id:'55-1',running:false,bench:false,blade_mask:31,active_blade:0,mode:1,
    set_rpm_x10:100,rune_rpm_x10:0,rotor_rpm:0,direction:1,motor_online:true,lease:true,blade_states:[0,0,0,0,0],blade_rings:[0,0,0,0,0],buffered_events:0,reason:'test'};
  const publishTimer=setInterval(()=>client.publish('urex/rune/telemetry',JSON.stringify(telemetry)),200);t.after(()=>clearInterval(publishTimer));
  client.publish('urex/rune/hit',JSON.stringify(e),{qos:1});client.publish('urex/rune/hit',JSON.stringify(e),{qos:1});
  const s=await until(async()=>{const s=await f.state();return s.hit_count===1&&s;});assert.equal(s.score,7);
  await new Promise(r=>client.subscribe('urex/rune/cmd',r));
  const received=new Promise(resolve=>client.on('message',(topic,bytes,packet)=>{if(topic.endsWith('/cmd'))resolve({command:JSON.parse(bytes),packet});}));
  await f.post('/api/heartbeat',{});assert.equal((await f.post('/api/command',{action:'start'})).status,202);
  const {command,packet}=await received;assert.equal(packet.retain,false);assert.ok(command.expires_at>Date.now());assert.equal(command.boot_id,55);
  client.publish('urex/rune/ack',JSON.stringify({kind:'ack',boot_id:55,id:command.id,result:'applied'}),{qos:1});
  await until(async()=>(await f.state()).last_command?.acknowledged);
  // A device may publish telemetry, but cannot inject a command into this broker.
  const disconnected=new Promise(r=>client.once('close',r));
  client.publish('urex/rune/cmd','{"forged":true}',{retain:true});await disconnected;
});
