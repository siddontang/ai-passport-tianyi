"""Check the production C3 keepalive; optionally replay it through a local gateway source."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gateway-source', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cloud = root / 'components/agent_link/src/transports/wifi/cloud'
production = (cloud / 'cloud_realtime.cc').read_text()
begin = production.index('        if (!sender.pending.Busy() && now - last_keepalive')
end = production.index('        SetWorkerPhase(3);', begin)
fixture = (root / 'tests/realtime_keepalive_test.cc').read_text()
build = root / '.cache/realtime-keepalive-tests'
build.mkdir(parents=True, exist_ok=True)
source = build / 'realtime_keepalive_test.cc'
source.write_text(fixture.replace('// INSERT_C3_KEEPALIVE', production[begin:end]))
binary = build / 'realtime_keepalive_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I' + str(cloud), str(source), '-o', str(binary)], check=True)
frames = json.loads(subprocess.check_output([str(binary)], text=True))
print('PASS: actual C3 keepalive branch over 180 seconds; masked Pong, bounded delay, busy writer and write failure')

if args.gateway_source:
    gateway = args.gateway_source.read_text()
    begin = gateway.index('  private heartbeat(connection: Connection): void {')
    begin = gateway.index('{', begin) + 1
    heartbeat = gateway[begin:gateway.index('\n  }', begin)]
    begin = gateway.index("    socket.on('pong', () => {")
    begin = gateway.index('{', begin) + 1
    on_pong = gateway[begin:gateway.index('\n    });', begin)]
    script = r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const heartbeat = new Function('connection', 'WebSocket', 'VOICE_CLOSE_CODES', input.heartbeat);
const pong = new Function('connection', input.on_pong);
function replay(opcodeOverride, stopAt = Infinity) {
  let now = 0, closedAt = null, index = 0;
  const owner = {now: () => now, heartbeatTimeoutMs: 30000};
  const socket = {readyState: 1, ping() {}, close() {closedAt = now; this.readyState = 3;}, terminate() {}};
  const connection = {socket, lastPongAt: 0};
  // Server Ping is queued behind a long reply. No automatic response is
  // delivered, matching the paced reader condition this regression targets.
  for (now = 0; now <= 225000 && closedAt === null; now += 10) {
    while (index < input.frames.length && input.frames[index][0] <= now) {
      const [at, opcode] = input.frames[index++];
      if (at <= stopAt && (opcodeOverride ?? opcode) === 10) pong.call(owner, connection);
    }
    if (now && now % 15000 === 0)
      heartbeat.call(owner, connection, {OPEN: 1}, {heartbeatTimeout: 4008});
    if (now === 180000 && stopAt === Infinity && opcodeOverride !== 9) assert.equal(closedAt, null);
  }
  return closedAt;
}
const oldPing = replay(9);
const currentPong = replay(null);
const stoppedClient = replay(null, 60000);
assert(oldPing !== null && oldPing <= 45000);
assert(currentPong !== null && currentPong > 180000); // Ends only after generated frames stop.
assert(stoppedClient !== null && stoppedClient <= 105000); // Real timeout still works.
console.log(JSON.stringify({old_ping_closed_ms: oldPing, pong_survived_ms: 180000, stopped_client_closed_ms: stoppedClient}));
'''
    snapshot = {'frames': frames, 'heartbeat': heartbeat, 'on_pong': on_pong,
                'gateway_sha256': hashlib.sha256(gateway.encode()).hexdigest()}
    (build / 'gateway-input.json').write_text(json.dumps(snapshot))
    (build / 'gateway-replay.cjs').write_text(script)
    result = subprocess.check_output(['node', str(build / 'gateway-replay.cjs'),
                                     str(build / 'gateway-input.json')], text=True)
    (build / 'gateway-result.json').write_text(result)
    print('PASS: unmodified gateway heartbeat/Pong bodies, simulated delayed receive:', result.strip())
