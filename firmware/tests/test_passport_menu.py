"""Render actual Passport menu/UI code with the existing CJK subtitle font."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
app = root / 'apps/ai-passport-wifi'
common = root / 'boards/common'
build = root / '.cache/passport-menu-tests'
build.mkdir(parents=True, exist_ok=True)
stubs = build / 'stubs'
(stubs / 'freertos').mkdir(parents=True, exist_ok=True)
headers = {
    'agent_link.h': '''#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, ESP_ERR_NO_MEM = 2;
enum { AGENT_LINK_PHASE_CONNECTED, AGENT_LINK_PHASE_READY, AGENT_LINK_PHASE_PAIRING };
struct agent_link_status_t { int phase = AGENT_LINK_PHASE_CONNECTED; char title[80]{}, hint[192]{}, code[16]{}; };
''',
    'st7789_lcd.h': '''#pragma once
#include <cstdint>
#include <cstring>
struct St7789Lcd {
    uint8_t stripe[1920]{}, pixels[240 * 320 * 2]{};
    unsigned stripes = 0;
    bool Ready() { return true; }
    uint8_t* StripeBuffer() { return stripe; }
    int FlushStripe(int y, int rows) { memcpy(pixels + y * 480, stripe, rows * 480); ++stripes; return 0; }
};
''',
    'esp_log.h': '''#pragma once
template <typename... Args> void TestLog(Args...) {}
#define ESP_LOGI(...) TestLog(__VA_ARGS__)
#define ESP_LOGE(...) TestLog(__VA_ARGS__)
''',
    'esp_timer.h': '#pragma once\n#include <cstdint>\ninline int64_t esp_timer_get_time() { return 1000000; }\n',
    'freertos/FreeRTOS.h': '#pragma once\nusing TickType_t = unsigned;\n#define pdMS_TO_TICKS(ms) (ms)\n#define pdPASS 1\n',
    'freertos/task.h': '''#pragma once
void vTaskDelay(unsigned ticks);
inline void vTaskDelete(void*) {}
inline int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, void*) { return 1; }
''',
}
for name, text in headers.items():
    (stubs / name).write_text(text)
fixture = (root / 'tests/passport_menu_test.cc').read_text()
production = (app / 'ai_passport_wifi.cc').read_text()
for marker, start, end in [('ADD', '    void Add(', '\n    void Menu()'),
                            ('PUBLISH', '    void Publish()', '\n    void UpdateCall(')]:
    begin = production.index(start)
    fixture = fixture.replace('// INSERT_' + marker, production[begin:production.index(end, begin)])
fixture = fixture.replace('// INSERT_UI', '#include "' + str(app / 'passport_wifi_ui.cc') + '"')
source = build / 'passport_menu_test.cc'
source.write_text(fixture)
objects = []
for path in [common / f'raster_montserrat_{size}.c' for size in (14, 20, 28)] + [app / 'passport_font.c']:
    target = build / (path.stem + '.o')
    subprocess.run(['cc', '-std=c11', '-I' + str(common), '-c', str(path), '-o', str(target)], check=True)
    objects.append(str(target))
binary = build / 'passport_menu_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g', '-fsanitize=address,undefined',
                '-I' + str(stubs), '-I' + str(app), '-I' + str(common), str(source),
                str(common / 'stripe_canvas.cc'), str(app / 'passport_avatar.cc'),
                str(app / 'passport_avatar_data.cc'), *objects, '-o', str(binary)], check=True)
subprocess.run([str(binary), str(build)], check=True, timeout=30)
