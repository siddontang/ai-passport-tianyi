"""Run the actual bounded caption scheduler with memory/UB sanitizers."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
cloud = root / 'components/agent_link/src/transports/wifi/cloud'
build = root / '.cache/realtime-captions-tests'
build.mkdir(parents=True, exist_ok=True)
binary = build / 'realtime_captions_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-g', '-I' + str(cloud),
                str(root / 'tests/realtime_captions_test.cc'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
