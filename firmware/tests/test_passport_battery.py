"""Exercise the production CW2017 driver and app polling with fault injection."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
board = root / 'boards/ai-passport'
build = root / '.cache/passport-battery-tests'
stubs = build / 'stubs'
headers = {
    'driver/i2c_master.h': '''#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2;
using i2c_master_bus_handle_t = void*;
using i2c_master_dev_handle_t = void*;
constexpr int I2C_ADDR_BIT_LEN_7=0;
struct i2c_device_config_t { int dev_addr_length, device_address, scl_speed_hz; };
int i2c_master_bus_add_device(void*, const i2c_device_config_t*, void**);
int i2c_master_bus_rm_device(void*);
int i2c_master_transmit_receive(void*, const uint8_t*, size_t, uint8_t*, size_t, int);
int i2c_master_transmit(void*, const uint8_t*, size_t, int);
''',
    'esp_log.h': '''#pragma once
template <typename... Args> void Log(Args...) {}
#define ESP_LOGI(...) Log(__VA_ARGS__)
''',
    'freertos/FreeRTOS.h': '#pragma once\n#define pdMS_TO_TICKS(ms) (ms)\n',
    'freertos/task.h': '#pragma once\nvoid vTaskDelay(unsigned);\n',
}
for name, text in headers.items():
    target = stubs / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)
fixture = (root / 'tests/passport_battery_test.cc').read_text()
app = (root / 'apps/ai-passport-wifi/ai_passport_wifi.cc').read_text()
start = app.index('    void UpdateBattery(')
fixture = fixture.replace('// INSERT_POLL', app[start:app.index('    void ControlLoop()', start)])
source = build / 'passport_battery_test.cc'
source.write_text(fixture)
binary = build / 'passport_battery_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(stubs), '-I' + str(board), str(source), str(board / 'ai_passport_battery.cc'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True, timeout=30)
