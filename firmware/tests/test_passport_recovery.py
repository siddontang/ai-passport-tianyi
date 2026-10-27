"""Run production worker, Passport call control and microphone code with fault injection."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
build = root / '.cache/passport-recovery-tests'
build.mkdir(parents=True, exist_ok=True)
app = root / 'apps/ai-passport-wifi'
cloud = root / 'components/agent_link/src/transports/wifi/cloud'
fixture = (root / 'tests/passport_recovery_test.cc').read_text()
sections = {
    'END_CALL': (app / 'ai_passport_wifi.cc', '    void EndCall() {', '\n    void Error('),
    'UPDATE_CALL': (app / 'ai_passport_wifi.cc', '    void UpdateCall(', '\n    void ControlLoop()'),
    'MIC_LOOP': (app / 'passport_wifi_audio.cc', 'void PassportWifiAudio::MicLoop()', None),
    'WORKER': (cloud / 'cloud_realtime.cc', 'void Worker(void*) {', '\n}  // namespace'),
    'RECOVERING': (cloud / 'cloud_realtime.cc', 'bool cloud_realtime_recovering(void)', '\n\nvoid cloud_realtime_cancel'),
    'PUBLIC_QUERY': (root / 'components/agent_link/src/core/agent_link.cpp',
                     'bool agent_link_realtime_recovering(void)', '\n// '),
}
for marker, (path, start, end) in sections.items():
    source = path.read_text()
    begin = source.index(start)
    stop = source.index(end, begin) if end else len(source)
    fixture = fixture.replace('// INSERT_' + marker, source[begin:stop])
source = build / 'passport_recovery_test.cc'
source.write_text(fixture)
binary = build / 'passport_recovery_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-I' + str(cloud), str(source), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=30)
