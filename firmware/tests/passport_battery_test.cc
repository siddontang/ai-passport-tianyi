#include "ai_passport_battery.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

uint8_t registers[256]{};
unsigned operations = 0, fail_at = 0, added = 0, removed = 0, delayed = 0;
bool ignore_wake = false;
std::vector<uint8_t> modes;
int fake_bus = 0, fake_device = 0;
bool Fail() { return ++operations == fail_at; }
int i2c_master_bus_add_device(void* bus, const i2c_device_config_t* cfg, void** out) {
    assert(bus == &fake_bus && cfg->device_address == 0x63 && cfg->scl_speed_hz == 100000);
    assert(cfg->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    if (Fail()) return ESP_FAIL;
    *out = &fake_device; ++added; return ESP_OK;
}
int i2c_master_bus_rm_device(void* dev) { assert(dev == &fake_device); ++removed; return ESP_OK; }
int i2c_master_transmit_receive(void* dev, const uint8_t* tx, size_t tx_size, uint8_t* rx, size_t n, int timeout) {
    assert(dev == &fake_device && tx_size == 1 && timeout > 0 && timeout <= 20);
    assert(*tx == 0x00 || *tx == 0x0B || *tx == 0x08 || *tx == 0x04);
    assert(n == (*tx == 0x04 ? 2u : 1u));
    if (Fail()) return ESP_FAIL;
    memcpy(rx, registers + *tx, n); return ESP_OK;
}
int i2c_master_transmit(void* dev, const uint8_t* tx, size_t n, int timeout) {
    assert(dev == &fake_device && n == 2 && timeout > 0 && timeout <= 20);
    // No profile, alert threshold, codec or other registers may be written.
    assert(tx[0] == 0x08 && (tx[1] == 0x30 || tx[1] == 0x00));
    if (Fail()) return ESP_FAIL;
    modes.push_back(tx[1]);
    if (!ignore_wake) registers[0x08] = tx[1];
    return ESP_OK;
}
void vTaskDelay(unsigned ms) { assert(ms <= 20); delayed += ms; }
void Reset() {
    memset(registers, 0, sizeof registers);
    registers[0] = 0xA0; registers[0x0B] = 0x80; registers[4] = 75;
    operations = fail_at = added = removed = delayed = 0; ignore_wake = false; modes.clear();
}
namespace passport_wifi {
int displayed = -2, updates = 0;
void SetBattery(int percent) { displayed = percent; ++updates; }
}
struct Polling {
    struct { AiPassportBattery battery; } hardware_;
    int64_t battery_at_ = -1;
    // INSERT_POLL
};
int main() {
    Reset(); AiPassportBattery battery;
    assert(battery.ReadPercent() == -1);
    assert(battery.Init(nullptr, 0x63) == ESP_ERR_INVALID_ARG && !operations);
    assert(battery.Init(&fake_bus, 0x63) == ESP_OK && added == 1 && modes.empty() && !delayed);
    assert(battery.Init(&fake_bus, 0x63) == ESP_OK && added == 1);
    for (unsigned soc : {0u, 1u, 20u, 21u, 99u, 100u, 101u, 255u}) {
        registers[4] = soc; registers[5] = 255;
        assert(battery.ReadPercent() == (soc <= 100 ? int(soc) : -1));
    }
    registers[4] = 70; fail_at = operations + 1;
    assert(battery.ReadPercent() == -1);
    assert(battery.ReadPercent() == 70); // A transient failure must recover.
    for (int i = 0; i < 5000; ++i) assert(battery.ReadPercent() == 70);
    assert(added == 1 && modes.empty());
    for (uint8_t mode : {0xF0, 0x30}) {
        Reset(); registers[8] = mode; AiPassportBattery sleeping;
        assert(sleeping.Init(&fake_bus, 0x63) == ESP_OK);
        assert((modes == std::vector<uint8_t>{0x30, 0}) && delayed == 30);
        assert(sleeping.ReadPercent() == 75);
    }
    Reset(); registers[0x0B] = 0; AiPassportBattery uncalibrated;
    assert(uncalibrated.Init(&fake_bus, 0x63) == ESP_ERR_INVALID_STATE);
    assert(modes.empty() && removed == 1 && uncalibrated.ReadPercent() == -1);
    Reset(); registers[8] = 0xF0; ignore_wake = true; AiPassportBattery stuck;
    assert(stuck.Init(&fake_bus, 0x63) == ESP_ERR_INVALID_STATE && removed == 1);
    assert(stuck.ReadPercent() == -1);
    // Fail every bus operation in detection/wake, then retry initialization.
    for (unsigned fail = 1; fail <= 7; ++fail) {
        Reset(); registers[8] = 0xF0; fail_at = fail; AiPassportBattery broken;
        assert(broken.Init(&fake_bus, 0x63) != ESP_OK && removed == added);
        assert(broken.ReadPercent() == -1);
        fail_at = 0;
        assert(broken.Init(&fake_bus, 0x63) == ESP_OK && broken.ReadPercent() == 75);
    }
    Reset(); Polling app; assert(app.hardware_.battery.Init(&fake_bus, 0x63) == ESP_OK);
    const unsigned setup_operations = operations;
    app.UpdateBattery(0); assert(passport_wifi::updates == 1 && passport_wifi::displayed == 75);
    for (int64_t t = 100000; t < 5000000; t += 100000) app.UpdateBattery(t);
    assert(passport_wifi::updates == 1 && operations == setup_operations + 1);
    fail_at = operations + 1; app.UpdateBattery(5000000);
    assert(passport_wifi::updates == 2 && passport_wifi::displayed == -1);
    registers[4] = 0; app.UpdateBattery(10000000);
    assert(passport_wifi::updates == 3 && passport_wifi::displayed == 0);
    puts("PASS: production CW2017 detection/wake, calibration preservation, failures, SOC boundaries and 5-second control polling");
}
