function normalizeCommand(body, telemetry, now, id) {
  const actions = {stop:0,start:1,set:2,target:3,reset:4};
  if (!Object.hasOwn(actions, body.action)) throw new Error('Unknown command');
  const mode = body.mode ?? telemetry.mode ?? 1;
  const rpm = body.rpm ?? (telemetry.set_rpm_x10 ?? 100) / 10;
  const direction = body.direction ?? (telemetry.direction === 0 ? 'ccw' : 'cw');
  const blade = body.blade ?? 1;
  if (!Number.isInteger(mode) || mode < 0 || mode > 2 || typeof rpm !== 'number' || !Number.isFinite(rpm) || rpm < 0 || rpm > 20 ||
      !['cw','ccw'].includes(direction) || !Number.isInteger(blade) || blade < 1 || blade > 5)
    throw new Error('Invalid mode, speed, direction, or blade');
  return {id,boot_id:telemetry.boot_id,action:actions[body.action],mode,rpm_x10:Math.round(rpm*10),
    direction:direction === 'cw' ? 1 : 0,blade,expires_at:now+2000};
}
module.exports = {normalizeCommand};
