#pragma once
#include "passport_language.h"
#include "esp_err.h"

namespace passport_wifi {
esp_err_t SaveLanguage(Language language);
// Stop networking before clearing only al_wifi. Caller restarts on success;
// failure restarts the existing link and leaves ownership/identity untouched.
esp_err_t ClearWifi();
}
