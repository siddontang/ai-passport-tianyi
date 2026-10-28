#pragma once
#include "stripe_canvas.h"

namespace passport_wifi {
// A shared header indicator, independent of UI language and transcript layout.
inline bool BatteryStripe(int y) { return y >= 0 && y < 32; }
void DrawBattery(raster::Canvas& canvas, int percent, int y);
}
