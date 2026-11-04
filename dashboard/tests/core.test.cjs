const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {Journal,validHit}=require('../lib/journal.cjs');
const {normalizeCommand}=require('../lib/commands.cjs');
const hit={kind:'hit',boot_id:12,session_id:'12-1',event_id:1,blade_id:3,ring:10,time_ms:100};
test('duplicate MQTT deliveries and process restart do not double-score',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'urex-journal-'));const file=path.join(dir,'hits.jsonl');
  let store=new Journal(file);assert.equal(store.append(hit),true);assert.equal(store.append(hit),false);
  store=new Journal(file);assert.equal(store.append(hit),false);assert.equal(store.session('12-1').reduce((s,e)=>s+e.ring,0),10);
  assert.equal(store.append({...hit,boot_id:13,session_id:'13-1'}),true);assert.equal(store.events.length,2);
});
test('incomplete last disk record is removed before replay and append',()=>{
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'urex-partial-'));const file=path.join(dir,'hits.jsonl');
  new Journal(file).append(hit);fs.appendFileSync(file,'{"interrupted":');
  const store=new Journal(file);assert.equal(store.events.length,1);store.append({...hit,event_id:2});
  assert.equal(new Journal(file).events.length,2);
});
test('invalid hit fields cannot enter scoreboard',()=>{
  for(const bad of[{ring:11},{ring:-1},{blade_id:6},{event_id:0},{session_id:'99-1'},{time_ms:'100'},{boot_id:0}])assert.equal(validHit({...hit,...bad}),false);
});
test('commands bind to current controller boot and expire in two seconds',()=>{
  const c=normalizeCommand({action:'start',mode:2,rpm:10,direction:'ccw',blade:1},{boot_id:42},1000,5);
  assert.deepEqual(c,{id:5,boot_id:42,action:1,mode:2,rpm_x10:100,direction:0,blade:1,expires_at:3000});
  for(const bad of[{rpm:Infinity},{rpm:21},{mode:3},{direction:'reverse'},{blade:0},{action:'reboot'}])assert.throws(()=>normalizeCommand({action:'start',...bad},{boot_id:42},1000,5));
});
