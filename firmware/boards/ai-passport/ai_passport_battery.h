#pragma once
#include "driver/i2c_master.h"

// CW2017 on the existing codec bus. Keep the device's calibrated cell profile.
// Init runs before tasks start; subsequent reads belong to the control task.
class AiPassportBattery {
public:
    esp_err_t Init(i2c_master_bus_handle_t bus, uint8_t address);
    int ReadPercent(); // 0..100, or -1 when unavailable/not ready.
private:
    esp_err_t Read(uint8_t reg, uint8_t* data, size_t size);
    esp_err_t WriteMode(uint8_t mode);
    i2c_master_dev_handle_t device_ = nullptr;
};
