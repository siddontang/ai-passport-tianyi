#pragma once
#include "agent_link.h"
#include "st7789_lcd.h"

namespace passport_wifi {
enum class View { Text, Portrait };
esp_err_t StartUi(St7789Lcd* lcd);
void SetStatus(const agent_link_status_t& status);
void SetContent(const char* title, const char* detail, const char* footer,
                View view = View::Text, int volume = -1);
void SetAnswer(const char* text);
void SetPlaybackLevel(uint8_t level);
void ClearPlaybackLevel();
}  // namespace passport_wifi
