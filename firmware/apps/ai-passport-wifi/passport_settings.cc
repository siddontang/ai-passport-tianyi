#include "passport_settings.h"
#include "agent_link.h"
#include "nvs.h"
#include "esp_log.h"

namespace passport_wifi {
esp_err_t SaveLanguage(Language language) {
    nvs_handle_t handle;
    esp_err_t result = nvs_open("passport_wifi", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_u8(handle, "language", static_cast<uint8_t>(language));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result == ESP_OK) SetLanguage(language);
    return result;
}

esp_err_t ClearWifi() {
    // Prevent a pending successful provisioning callback from saving the old
    // credentials again between the erase and reboot. Never call forget(),
    // which also removes the platform credential.
    agent_link_stop();
    nvs_handle_t handle;
    esp_err_t result = nvs_open("al_wifi", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_erase_all(handle);
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        const esp_err_t restart = agent_link_start();
        if (restart != ESP_OK) ESP_LOGW("Passport.settings", "link restart failed: %s", esp_err_to_name(restart));
    }
    return result;
}
}

// Optional provisioning hook; other applications keep the component default.
extern "C" bool al_wifi_prov_use_english(void) {
    return passport_wifi::GetLanguage() == passport_wifi::Language::English;
}
