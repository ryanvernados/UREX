const http=require('node:http'),net=require('node:net'),fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const mqtt=require('mqtt'),createAedes=require('aedes');
const {Journal,validHit}=require('./lib/journal.cjs');
const {normalizeCommand}=require('./lib/commands.cjs');
const demo=process.argv.includes('--demo');
const env=process.env;
const password=demo?crypto.randomBytes(18).toString('hex'):env.MQTT_PASSWORD;
const webPassword=demo?'':env.DASHBOARD_PASSWORD;
if(!demo&&(!password||password.includes('CHANGE_ME')||!webPassword||webPassword.includes('CHANGE_ME'))){
  console.error('Copy .env.example to .env and set MQTT_PASSWORD and DASHBOARD_PASSWORD.');process.exit(1);
}
const deviceUser=env.MQTT_USER||'urex-gateway',webUser=env.DASHBOARD_USER||'operator';
const httpHost=demo?'127.0.0.1':env.HTTP_HOST||'127.0.0.1',httpPort=Number(env.HTTP_PORT||8080);
const brokerHost=demo?'127.0.0.1':env.MQTT_HOST||'0.0.0.0',brokerPort=Number(demo?(env.MQTT_PORT||1884):(env.MQTT_PORT||1883));
const dataDir=env.UREX_DATA_DIR||path.join(__dirname,'data',demo?'demo':'live');
const journal=new Journal(path.join(dataDir,'hits.jsonl'));
const token=crypto.randomBytes(24).toString('hex'),backendPassword=crypto.randomBytes(24).toString('hex');
const backendId='urex-dashboard-'+crypto.randomBytes(6).toString('hex');
const equal=(a,b)=>{const x=Buffer.from(a||''),y=Buffer.from(b||'');return x.length===y.length&&crypto.timingSafeEqual(x,y);};
const broker=createAedes();
broker.authenticate=(client,user,pass,done)=>{
  if(user==='internal-dashboard'&&client.id===backendId&&equal(pass?.toString(),backendPassword))client.urexRole='backend';
  else if(user===deviceUser&&equal(pass?.toString(),password))client.urexRole='device';
  else return done(null,false);
  done(null,true);
};
const devicePublish=new Set(['urex/rune/telemetry','urex/rune/hit','urex/rune/ack','urex/rune/gateway']);
const deviceSubscribe=new Set(['urex/rune/cmd','urex/rune/clock','urex/rune/lease','urex/rune/event_ack']);
broker.authorizePublish=(client,packet,done)=>{
  if(!client)return done(null);
  const allowed=client.urexRole==='backend'?packet.topic.startsWith('urex/rune/'):devicePublish.has(packet.topic);
  if(!allowed||packet.payload.length>4096||((packet.topic==='urex/rune/cmd'||packet.topic==='urex/rune/lease')&&packet.retain))return done(new Error('Topic not permitted'));
  done(null);
};
broker.authorizeSubscribe=(client,sub,done)=>{
  const allowed=client.urexRole==='backend'?sub.topic==='urex/rune/#':deviceSubscribe.has(sub.topic);
  done(null,allowed?sub:null);
};
broker.on('clientError',(_client,error)=>console.error('MQTT client:',error.message));
const tcp=net.createServer(broker.handle);
let telemetry=null,telemetryAt=0,gateway=false,operatorAt=0,lastCommand=null;
const pending=new Map(),streams=new Set();
let client,simulator;
function snapshot(){
  const events=journal.session(telemetry?.session_id);
  return {demo,mqtt:!!client?.connected,gateway,controller_online:!!telemetry&&Date.now()-telemetryAt<1500,
    telemetry,last_command:lastCommand,score:events.reduce((sum,e)=>sum+e.ring,0),hit_count:events.length,
    events:events.slice(-200).reverse(),control_token:token,server_time:Date.now()};
}
function broadcast(){const text=`data: ${JSON.stringify(snapshot())}\n\n`;for(const res of streams){if(res.writableLength>65536){res.destroy();streams.delete(res);}else res.write(text);}}
function publish(topic,data,qos=0){if(client?.connected)client.publish('urex/rune/'+topic,JSON.stringify(data),{qos,retain:false});}
function validTelemetry(d){return d?.kind==='telemetry'&&Number.isInteger(d.boot_id)&&d.boot_id>0&&d.boot_id<=0xffffffff&&
  typeof d.session_id==='string'&&/^\d{1,10}-\d{1,10}$/.test(d.session_id)&&Number(d.session_id.split('-')[0])===d.boot_id&&
  typeof d.running==='boolean'&&typeof d.bench==='boolean'&&Number.isInteger(d.blade_mask)&&d.blade_mask>=0&&d.blade_mask<=31&&
  Number.isInteger(d.active_blade)&&d.active_blade>=0&&d.active_blade<=5&&Number.isFinite(d.rune_rpm_x10)&&
  [d.blade_states,d.blade_rings].every(a=>Array.isArray(a)&&a.length===5&&a.every(n=>Number.isInteger(n)&&n>=0&&n<=255));}
tcp.listen(brokerPort,brokerHost,()=>{
  const url=`mqtt://127.0.0.1:${brokerPort}`;
  client=mqtt.connect(url,{clientId:backendId,username:'internal-dashboard',password:backendPassword,clean:true,reconnectPeriod:500});
  client.on('error',error=>console.error('MQTT backend:',error.message));
  client.on('connect',()=>client.subscribe('urex/rune/#',{qos:1}));
  client.on('message',(topic,payload)=>{
    let d;try{d=JSON.parse(payload);}catch{return;}
    if(topic==='urex/rune/gateway')gateway=d.online===true;
    if(topic==='urex/rune/telemetry'&&validTelemetry(d)){telemetry=d;telemetryAt=Date.now();}
    if(topic==='urex/rune/hit'&&validHit(d)){
      try{journal.append(d);publish('event_ack',{boot_id:d.boot_id,event_id:d.event_id},1);}
      catch(error){console.error('Hit not acknowledged:',error.message);}
    }
    if(topic==='urex/rune/ack'){
      const command=pending.get(d.id);
      if(command&&command.boot_id===d.boot_id&&typeof d.result==='string'&&/^[a-z_]{1,40}$/.test(d.result)){
        pending.delete(d.id);lastCommand={...command,result:d.result,acknowledged:true};
      }
    }
    broadcast();
  });
  if(demo)simulator=require('./lib/simulator.cjs').createSimulator(url,{username:deviceUser,password});
  console.log(`UREX ${demo?'DEMO (no hardware connection)':'LIVE'} dashboard: http://${httpHost}:${httpPort}`);
  console.log(`MQTT listening on ${brokerHost}:${brokerPort}. Hit journal: ${dataDir}`);
});
tcp.on('error',error=>{console.error(error.message);process.exitCode=1;shutdown();});
const timer=setInterval(()=>{
  const now=Date.now();publish('clock',{time_ms:now});
  if(now-operatorAt<2500&&telemetry&&now-telemetryAt<1500&&gateway)publish('lease',{time_ms:now,ttl_ms:1500});
  for(const [id,command]of pending){if(now>command.expires_at+1000){pending.delete(id);lastCommand={...command,result:'delivery_timeout',acknowledged:false};}}
  broadcast();
},500);
function json(res,status,data){res.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(data));}
const types={'.html':'text/html; charset=utf-8','.css':'text/css','.js':'text/javascript'};
const server=http.createServer(async(req,res)=>{
  res.setHeader('X-Content-Type-Options','nosniff');
  res.setHeader('Content-Security-Policy',"default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  if(!demo){
    const expected='Basic '+Buffer.from(`${webUser}:${webPassword}`).toString('base64');
    if(!equal(req.headers.authorization,expected)){res.writeHead(401,{'WWW-Authenticate':'Basic realm="UREX"'});res.end('Authentication required');return;}
  }
  const url=new URL(req.url,'http://localhost');
  if(req.method==='GET'&&url.pathname==='/api/state')return json(res,200,snapshot());
  if(req.method==='GET'&&url.pathname==='/api/events'){
    res.writeHead(200,{'Content-Type':'text/event-stream','Cache-Control':'no-cache','Connection':'keep-alive'});
    streams.add(res);res.write(`data: ${JSON.stringify(snapshot())}\n\n`);req.on('close',()=>streams.delete(res));return;
  }
  if(req.method==='GET'&&url.pathname==='/api/export.csv'){
    const header='session_id,event_id,blade_id,ring,controller_time_ms,received_at';
    const rows=journal.events.map(e=>[e.session_id,e.event_id,e.blade_id,e.ring,e.time_ms,e.received_at].join(','));
    res.writeHead(200,{'Content-Type':'text/csv','Content-Disposition':'attachment; filename="urex-hits.csv"'});res.end([header,...rows].join('\r\n'));return;
  }
  if(req.method==='POST'&&url.pathname.startsWith('/api/')){
    if(!equal(req.headers['x-urex-token'],token))return json(res,403,{error:'Refresh the dashboard before issuing controls'});
    if(req.headers.origin&&req.headers.origin!==`http://${req.headers.host}`)return json(res,403,{error:'Cross-origin controls rejected'});
    let body;
    try{let text='';for await(const chunk of req){text+=chunk;if(text.length>4096)throw new Error('Request too large');}body=JSON.parse(text||'{}');}
    catch{return json(res,400,{error:'Invalid JSON request'});}
    if(!body||typeof body!=='object'||Array.isArray(body))return json(res,400,{error:'Expected a JSON object'});
    if(url.pathname==='/api/heartbeat'){operatorAt=Date.now();return json(res,200,{ok:true});}
    if(url.pathname==='/api/demo-hit'){
      if(!demo)return json(res,403,{error:'Simulation is disabled for hardware mode'});
      if(!Number.isInteger(body.blade)||!Number.isInteger(body.ring)||body.blade<1||body.blade>5||body.ring<1||body.ring>10)return json(res,400,{error:'Invalid hit'});
      return json(res,200,{accepted:simulator?.hit(body.blade,body.ring)||false});
    }
    if(url.pathname==='/api/command'){
      if(!client?.connected||!gateway||!telemetry||(body.action!=='stop'&&Date.now()-telemetryAt>1500))return json(res,409,{error:'Gateway/controller offline; command not sent'});
      try{
        const command=normalizeCommand(body,telemetry,Date.now(),crypto.randomInt(1,0xffffffff));
        pending.set(command.id,command);lastCommand={...command,result:'awaiting_controller',acknowledged:false};
        publish('cmd',command);broadcast();return json(res,202,{id:command.id});
      }catch(error){return json(res,400,{error:error.message});}
    }
    return json(res,404,{error:'Not found'});
  }
  const staticFiles={'/':'index.html','/app.js':'app.js','/style.css':'style.css'};
  if(req.method==='GET'&&staticFiles[url.pathname]){
    const file=path.join(__dirname,'public',staticFiles[url.pathname]);
    res.writeHead(200,{'Content-Type':types[path.extname(file)],'Cache-Control':'no-cache'});fs.createReadStream(file).pipe(res);return;
  }
  res.writeHead(404);res.end('Not found');
});
server.listen(httpPort,httpHost);
server.on('error',error=>{console.error(error.message);process.exitCode=1;shutdown();});
let closing=false;
function shutdown(){if(closing)return;closing=true;clearInterval(timer);simulator?.close();for(const res of streams)res.end();client?.end(true);server.close();tcp.close();broker.close();}
process.on('SIGINT',shutdown);process.on('SIGTERM',shutdown);
