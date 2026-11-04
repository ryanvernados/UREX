const fs = require('node:fs');
const path = require('node:path');
function validHit(e) {
  const uint = (v) => Number.isInteger(v) && v >= 0 && v <= 0xffffffff;
  return e && e.kind === 'hit' && uint(e.boot_id) && e.boot_id > 0 && uint(e.event_id) && e.event_id > 0 &&
    Number.isInteger(e.blade_id) && e.blade_id >= 1 && e.blade_id <= 5 &&
    Number.isInteger(e.ring) && e.ring >= 1 && e.ring <= 10 && uint(e.time_ms) &&
    typeof e.session_id === 'string' && /^\d{1,10}-\d{1,10}$/.test(e.session_id) &&
    Number(e.session_id.split('-')[0]) === e.boot_id;
}
class Journal {
  constructor(file) {
    this.file = file; this.events = []; this.keys = new Set();
    fs.mkdirSync(path.dirname(file), {recursive: true});
    if (!fs.existsSync(file)) fs.writeFileSync(file, '');
    let text = fs.readFileSync(file, 'utf8');
    // Remove an interrupted final append before accepting more events.
    if (text && !text.endsWith('\n')) {
      text = text.slice(0, text.lastIndexOf('\n') + 1);
      fs.truncateSync(file, Buffer.byteLength(text));
    }
    for (const line of text.split('\n').filter(Boolean)) {
      const event = JSON.parse(line);
      if (!validHit(event)) throw new Error('Invalid record in hit journal; preserve the file and investigate.');
      const key = `${event.boot_id}/${event.event_id}`;
      if (!this.keys.has(key)) { this.keys.add(key); this.events.push(event); }
    }
  }
  append(input) {
    if (!validHit(input)) throw new Error('Invalid hit');
    const key = `${input.boot_id}/${input.event_id}`;
    if (this.keys.has(key)) return false;
    const event = {kind:'hit',boot_id:input.boot_id,session_id:input.session_id,event_id:input.event_id,
      blade_id:input.blade_id,ring:input.ring,time_ms:input.time_ms,received_at:new Date().toISOString()};
    const fd = fs.openSync(this.file, 'a');
    try { fs.writeSync(fd, JSON.stringify(event) + '\n'); fs.fsyncSync(fd); }
    finally { fs.closeSync(fd); }
    this.keys.add(key); this.events.push(event); return true;
  }
  session(id) { return this.events.filter(e => e.session_id === id); }
}
module.exports = {Journal, validHit};
