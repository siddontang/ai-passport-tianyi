// Register map and wake sequence from FoloToy/ai-passport's CW2017 BSP (MIT).
// Unlike the reference initializer, this driver never replaces the cell profile.
#include "ai_passport_battery.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "Passport.battery";
constexpr uint8_t kVersion = 0x00, kSoc = 0x04, kMode = 0x08, kAlert = 0x0B;
constexpr int kTimeoutMs = 20;
}

esp_err_t AiPassportBattery::Read(uint8_t reg, uint8_t* data, size_t size) {
    return i2c_master_transmit_receive(device_, &reg, 1, data, size, kTimeoutMs);
}
esp_err_t AiPassportBattery::WriteMode(uint8_t mode) {
    const uint8_t command[] = {kMode, mode};
    return i2c_master_transmit(device_, command, sizeof command, kTimeoutMs);
}
esp_err_t AiPassportBattery::Init(i2c_master_bus_handle_t bus, uint8_t address) {
    if (device_) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 100000;
    esp_err_t result = i2c_master_bus_add_device(bus, &config, &device_);
    if (result != ESP_OK) return result;

    uint8_t version = 0, alert = 0, mode = 0;
    result = Read(kVersion, &version, 1);
    if (result == ESP_OK) result = Read(kAlert, &alert, 1);
    // An unconfigured gauge cannot supply a trustworthy percentage. Do not
    // install another cell's calibration just to make the indicator appear.
    if (result == ESP_OK && !(alert & 0x80)) result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = Read(kMode, &mode, 1);
    if (result == ESP_OK && mode != 0) {
        result = WriteMode(0x30);
        if (result == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            result = WriteMode(0x00);
        }
        if (result == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            result = Read(kMode, &mode, 1);
            if (result == ESP_OK && mode != 0) result = ESP_ERR_INVALID_STATE;
        }
    }
    if (result != ESP_OK) {
        (void)i2c_master_bus_rm_device(device_);
        device_ = nullptr;
        return result;
    }
    ESP_LOGI(TAG, "CW2017 version=0x%02x; retaining existing cell profile", version);
    return ESP_OK;
}
int AiPassportBattery::ReadPercent() {
    if (!device_) return -1;
    uint8_t soc[2]{};
    if (Read(kSoc, soc, sizeof soc) != ESP_OK || soc[0] > 100) return -1;
    return soc[0];
}
