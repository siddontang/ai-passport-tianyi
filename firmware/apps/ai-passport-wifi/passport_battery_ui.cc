#include "passport_battery_ui.h"
#include <cstdio>
#include <cstring>

namespace passport_wifi {
void DrawBattery(raster::Canvas& canvas, int percent, int y) {
    const bool valid = percent >= 0 && percent <= 100;
    const uint32_t color = !valid ? 0x8A9DA4 : percent <= 20 ? 0xFF997D : 0x8AE1D8;
    // Outline, terminal and fill; zero must remain visually empty.
    canvas.Box({156, y + 3, 21, 1}, color);
    canvas.Box({156, y + 14, 21, 1}, color);
    canvas.Box({156, y + 3, 1, 12}, color);
    canvas.Box({176, y + 3, 1, 12}, color);
    canvas.Box({178, y + 7, 2, 4}, color);
    if (valid && percent > 0) canvas.Box({158, y + 5, (17 * percent + 99) / 100, 8}, color);
    char text[8];
    if (valid) snprintf(text, sizeof text, "%d%%", percent);
    else snprintf(text, sizeof text, "--%%");
    canvas.Text(raster_montserrat_14, 186, y, text, text + strlen(text), color);
}
}
