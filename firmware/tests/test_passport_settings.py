"""Exercise production menu actions, NVS settings and portal locale rendering."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
app = root / 'apps/ai-passport-wifi'
build = root / '.cache/passport-settings-tests'
stubs = build / 'stubs'
stubs.mkdir(parents=True, exist_ok=True)
headers = {
    'esp_err.h': '''#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NVS_NOT_FOUND=1;
inline const char* esp_err_to_name(int) { return "fixture"; }
''',
    'agent_link.h': '#pragma once\nvoid agent_link_stop();\nint agent_link_start();\n',
    'esp_log.h': '''#pragma once
template <typename... Args> void TestLog(Args...) {}
#define ESP_LOGI(...) TestLog(__VA_ARGS__)
#define ESP_LOGW(...) TestLog(__VA_ARGS__)
''',
    'nvs.h': '''#pragma once
#include <cstdint>
#include <cstddef>
using nvs_handle_t = int;
constexpr int NVS_READWRITE=1, NVS_READONLY=0;
int nvs_open(const char*, int, nvs_handle_t*);
int nvs_set_u8(nvs_handle_t, const char*, uint8_t);
int nvs_commit(nvs_handle_t);
int nvs_erase_all(nvs_handle_t);
void nvs_close(nvs_handle_t);
int nvs_get_u8(nvs_handle_t, const char*, uint8_t*);
int nvs_get_i32(nvs_handle_t, const char*, int32_t*);
int nvs_get_str(nvs_handle_t, const char*, char*, size_t*);
''',
}
for name, text in headers.items():
    (stubs / name).write_text(text)
fixture = (root / 'tests/passport_settings_test.cc').read_text()
production = (app / 'ai_passport_wifi.cc').read_text()
for marker, start, end in [
    ('LOAD', '    void Load()', '\n    void Save()'),
    ('PAGES', '    void SetPage(', '\n    bool Fetch('),
    ('HANDLE', '    void Handle(', '\n    void Publish()'),
]:
    begin = production.index(start)
    fixture = fixture.replace('// INSERT_' + marker, production[begin:production.index(end, begin)])
portal = (root / 'components/agent_link/src/transports/wifi/wifi_provision.cpp').read_text()
for marker, start, end in [
    ('REPLACE', 'void ReplaceAll(', '\n}'),
    ('PORTAL', 'void ApplyLocale(', '\nesp_err_t FaviconGet('),
]:
    begin = portal.index(start)
    stop = portal.index(end, begin) + (2 if marker == 'REPLACE' else 0)
    fixture = fixture.replace('// INSERT_' + marker, portal[begin:stop])
source = build / 'passport_settings_test.cc'
source.write_text(fixture)
binary = build / 'passport_settings_test'
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                '-I' + str(stubs), '-I' + str(app), '-I' + str(root / 'boards/common'),
                str(source), str(app / 'passport_settings.cc'), str(app / 'passport_language.cc'),
                '-o', str(binary)], check=True)
subprocess.run([str(binary), str(root / 'components/agent_link/portal')], check=True, timeout=30)
