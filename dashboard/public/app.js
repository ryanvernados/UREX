const $=id=>document.getElementById(id);
let state=null,token='',lastPoint=0,lastError='';
const samples=[];
const svgNS='http://www.w3.org/2000/svg';
function svg(tag,attrs){const node=document.createElementNS(svgNS,tag);for(const[k,v]of Object.entries(attrs))node.setAttribute(k,v);return node;}
const bladeNodes=[];
for(let i=0;i<5;i++){
  const angle=(i*72-90)*Math.PI/180,cx=240+Math.cos(angle)*123,cy=195+Math.sin(angle)*123;
  const group=svg('g',{}),line=svg('line',{x1:240,y1:195,x2:cx,y2:cy,class:'arm-line'});
  const shape=svg('rect',{x:cx-30,y:cy-27,width:60,height:54,rx:13,class:'blade-shape offline'});
  const label=svg('text',{x:cx,y:cy-3,'text-anchor':'middle',class:'blade-label'});label.textContent=`0${i+1}`;
  const ring=svg('text',{x:cx,y:cy+15,'text-anchor':'middle',class:'blade-ring'});ring.textContent='—';
  group.append(line,shape,label,ring);$('blades').append(group);bladeNodes.push({shape,ring});
  const status=document.createElement('div'),name=document.createElement('span'),value=document.createElement('b');
  name.textContent=`BLADE 0${i+1}`;value.textContent='Offline';status.append(name,value);$('blade-list').append(status);
}
async function api(path,body){const response=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json','X-Urex-Token':token},body:JSON.stringify(body)});const data=await response.json();if(!response.ok)throw new Error(data.error||'Request failed');return data;}
function settings(){return{mode:Number($('mode').value),rpm:Number($('rpm').value),direction:$('direction').value,blade:Number($('blade').value)};}
async function command(action){try{lastError='';await api('/api/command',{action,...settings()});}catch(error){lastError=error.message;render(state);}}
for(const[action,id]of[['start','start'],['stop','stop'],['set','apply'],['target','target'],['reset','reset']])$(id).onclick=()=>command(action);
$('demo-hit').onclick=async()=>{try{const result=await api('/api/demo-hit',{blade:Number($('blade').value),ring:Number($('demo-ring').value)});if(!result.accepted)throw new Error('Arm the selected blade before sending a hit.');lastError='';}catch(error){lastError=error.message;render(state);}};
function render(s){
  if(!s)return;state=s;token=s.control_token;
  const t=s.telemetry,online=s.controller_online&&s.gateway;
  $('environment').textContent=s.demo?'SIMULATION / NO HARDWARE':'LOCAL HARDWARE';
  $('connection').textContent=online?'● System connected':'○ Controller offline';
  $('notice').textContent=s.demo?'Simulation mode. Generated telemetry and hits never control physical hardware.':
    'Example setup: 20T motor / 60T rune pulleys, 19:1 gearbox, 30 LEDs per chain. Verify rune_config.h and wiring before motor power.';
  $('clock').textContent=new Date().toLocaleTimeString();
  $('score').textContent=s.score;$('hits').textContent=s.hit_count;
  $('average').textContent=s.hit_count?`${(s.score/s.hit_count).toFixed(1)} average ring score`:'No hits this session';
  $('speed').textContent=online?(t.rune_rpm_x10/10).toFixed(1):'—';
  $('rotor').textContent=online&&t.motor_online?`${t.rotor_rpm} rotor RPM`:'Motor feedback unavailable';
  const count=online?[0,1,2,3,4].filter(i=>t.blade_mask&(1<<i)).length:0;
  $('online').textContent=`${count} / 5`;$('session').textContent=t?`Session ${t.session_id}`:'Waiting for a session';
  $('run-state').textContent=!online?'OFFLINE':t.running?'ROTATING':t.bench?'BLADE TEST':'STOPPED';
  $('run-state').className='state'+(online&&(t.running||t.bench)?' running':'');
  bladeNodes.forEach((b,i)=>{
    const connected=online&&(t.blade_mask&(1<<i)),mode=connected?(t.blade_states[i]&3):0;
    const label=!connected?'Offline':mode===2?'Target':mode===3?'Hit':mode===1?'Idle':'Off';
    b.shape.setAttribute('class','blade-shape '+(!connected?'offline':mode===2?'target':mode===3?'hit':''));
    b.ring.textContent=connected&&t.blade_rings[i]?`${t.blade_rings[i]} pts`:connected?'READY':'—';
    $('blade-list').children[i].querySelector('b').textContent=label;
  });
  const pending=s.last_command?.result==='awaiting_controller';
  $('start').disabled=!online||!t.lease||!t.motor_online||count!==5||t.running||t.bench||pending;
  $('target').disabled=!online||!t.lease||t.running||pending;$('apply').disabled=!online||!t.lease||pending;
  $('stop').disabled=!s.gateway||!t;$('reset').disabled=!online||t.running||t.bench||pending;
  $('demo-controls').hidden=!s.demo;
  const ack=s.last_command;
  $('command-status').textContent=lastError||(ack?`Command ${ack.id} · ${ack.result.replaceAll('_',' ')}${ack.acknowledged?' · controller acknowledged':''}`:'No command sent.');
  $('command-status').className='command-status'+(lastError||(ack&&!['applied','awaiting_controller'].includes(ack.result))?' error':'');
  $('health').textContent=online?`${t.reason.replaceAll('_',' ')} · ${t.buffered_events} buffered hits · control lease ${t.lease?'active':'inactive'}`:'Controls wait for fresh telemetry';
  $('empty').hidden=!!s.events.length;
  const signature=s.events.map(e=>`${e.boot_id}/${e.event_id}`).join(',');
  if($('history').dataset.signature!==signature){
    $('history').dataset.signature=signature;$('history').replaceChildren();
    for(const e of s.events.slice(0,25)){
      const row=document.createElement('tr');
      for(const value of[`#${String(e.event_id).padStart(4,'0')}`,`Blade 0${e.blade_id}`,`${e.ring} / 10`,`${(e.time_ms/1000).toFixed(3)} s`,new Date(e.received_at).toLocaleTimeString()]){
        const cell=document.createElement('td');cell.textContent=value;row.append(cell);
      }
      $('history').append(row);
    }
  }
  const now=Date.now();if(now-lastPoint>240){lastPoint=now;samples.push({at:now,value:online?t.rune_rpm_x10/10:null});while(samples.length&&now-samples[0].at>60000)samples.shift();draw();}
}
function draw(){
  const canvas=$('speed-chart'),ratio=devicePixelRatio||1,width=canvas.clientWidth,height=130;
  canvas.width=width*ratio;canvas.height=height*ratio;const c=canvas.getContext('2d');c.scale(ratio,ratio);
  const left=32,right=width-10,top=8,bottom=106,now=Date.now(),mapY=v=>top+(20-v)/40*(bottom-top);
  c.font='9px Segoe UI';c.fillStyle='#9aabb3';c.lineWidth=1;
  for(const value of[-20,-10,0,10,20]){const y=mapY(value);c.strokeStyle=value===0?'#cfdcde':'#edf2f4';c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke();c.fillText(String(value),3,y+3);}
  c.strokeStyle='#168c83';c.lineWidth=2;c.beginPath();let pen=false;
  for(const p of samples){if(p.value===null){pen=false;continue;}const x=right-(now-p.at)/60000*(right-left),y=mapY(Math.max(-20,Math.min(20,p.value)));if(!pen)c.moveTo(x,y);else c.lineTo(x,y);pen=true;}c.stroke();
  c.fillText('−60s',left,125);c.fillText('−30s',(left+right)/2-10,125);c.fillText('now',right-18,125);
}
addEventListener('resize',draw);
async function init(){try{const response=await fetch('/api/state');render(await response.json());
  const events=new EventSource('/api/events');events.onmessage=e=>render(JSON.parse(e.data));events.onerror=()=>{$('connection').textContent='○ Dashboard reconnecting…';for(const id of['start','target','apply'])$(id).disabled=true;};
  const heartbeat=()=>api('/api/heartbeat',{}).catch(()=>{});heartbeat();setInterval(heartbeat,750);
}catch(error){$('notice').textContent=error.message;}}
init();
