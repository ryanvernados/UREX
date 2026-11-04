const mqtt = require('mqtt');
function createSimulator(url, credentials) {
  const boot = require('node:crypto').randomInt(1,0xffffffff), started = Date.now();
  let run = 0, eventId = 0, running = false, bench = false, active = 0, mode = 1, rpm = 100, direction = 1;
  let leaseAt = 0, runAt = 0, lastAuto = 0;
  const done = new Set(), acknowledged = new Map(), pendingEvents = new Map();
  const rings = [0,0,0,0,0], states = [0,0,0,0,0];
  const client = mqtt.connect(url,{...credentials,clientId:'urex-gateway-simulator',clean:true,reconnectPeriod:500,
    will:{topic:'urex/rune/gateway',payload:'{"online":false}',retain:true,qos:1}});
  const publish = (topic,data,qos=0) => client.connected && client.publish(`urex/rune/${topic}`,JSON.stringify(data),{qos,retain:false});
  function stop(reason) { running=false;bench=false;active=0;states.fill(0);currentReason=reason; }
  let currentReason='boot_stopped';
  client.on('connect',()=>{client.subscribe(['urex/rune/cmd','urex/rune/lease','urex/rune/event_ack']);
    client.publish('urex/rune/gateway','{"online":true,"simulated":true}',{retain:true,qos:1});});
  client.on('error',()=>{});
  client.on('message',(topic,payload)=>{
    let d;try{d=JSON.parse(payload);}catch{return;}
    if(topic.endsWith('/lease')) { if(Math.abs(Date.now()-d.time_ms)<1500)leaseAt=Date.now();return; }
    if(topic.endsWith('/event_ack')) { if(d.boot_id===boot)pendingEvents.delete(d.event_id);return; }
    if(!topic.endsWith('/cmd')||d.boot_id!==boot||d.expires_at<Date.now())return;
    let result='applied';
    if(!acknowledged.has(d.id)) {
      if(d.action===0)stop('operator_stop');
      else if(Date.now()-leaseAt>=2000) result='control_lease_expired';
      else if(d.action===1) {
        if(running||bench)result='already_active';
        else{++run;running=true;bench=false;done.clear();rings.fill(0);states.fill(1);active=1;states[0]=2;
          mode=d.mode;rpm=d.rpm_x10;direction=d.direction;runAt=Date.now();currentReason='running';lastAuto=Date.now();}
      } else if(d.action===2){mode=d.mode;rpm=d.rpm_x10;direction=d.direction;}
      else if(d.action===3){if(running)result='stop_before_blade_test';else{++run;bench=true;active=d.blade;done.clear();states.fill(0);states[active-1]=2;lastAuto=Date.now();currentReason='blade_test';}}
      else if(d.action===4){if(running||bench)result='stop_before_reset';else ++run;}
      acknowledged.set(d.id,result);
      if(acknowledged.size>64)acknowledged.delete(acknowledged.keys().next().value);
    } else result=acknowledged.get(d.id);
    publish('ack',{kind:'ack',boot_id:boot,id:d.id,result},1);
  });
  function hit(blade=active,ring=8){
    if((!running&&!bench)||!active||blade!==active||ring<1||ring>10)return false;
    rings[blade-1]=ring;states[blade-1]=3;done.add(blade);
    const event={kind:'hit',boot_id:boot,session_id:`${boot}-${run}`,event_id:++eventId,blade_id:blade,ring,time_ms:(Date.now()-started)>>>0};
    pendingEvents.set(event.event_id,event);publish('hit',event,1);
    if(bench)active=0;
    else{active=[1,2,3,4,5].find(i=>!done.has(i))||0;if(active)states[active-1]=2;else stop('cycle_complete');}
    return true;
  }
  const timer=setInterval(()=>{
    if(!client.connected)return;
    if((running||bench)&&Date.now()-leaseAt>2000)stop('control_link_lost');
    let actual=0;
    if(running){actual=mode===1?100:mode===2?Math.round((0.9*Math.sin(1.94*(Date.now()-runAt)/1000)+1.19)*95.492966):rpm;actual*=direction?1:-1;}
    publish('telemetry',{kind:'telemetry',boot_id:boot,session_id:`${boot}-${run}`,running,bench,mode,set_rpm_x10:rpm,
      rune_rpm_x10:actual,rotor_rpm:Math.round(actual*5.7),direction,motor_online:true,lease:Date.now()-leaseAt<2000,
      blade_mask:31,active_blade:active,blade_states:states,blade_rings:rings,buffered_events:pendingEvents.size,reason:currentReason});
    for(const event of pendingEvents.values())publish('hit',event,1);
    if(running&&Date.now()-lastAuto>2000){lastAuto=Date.now();hit(active,6+(eventId%5));}
  },250);
  return {hit,close(){clearInterval(timer);client.end(true);}};
}
module.exports={createSimulator};
