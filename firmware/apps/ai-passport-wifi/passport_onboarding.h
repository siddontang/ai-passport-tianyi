#pragma once
#include "agent_link.h"
#include "passport_language.h"
#include "stripe_canvas.h"

namespace passport_wifi {
uint32_t CodeRemaining(const agent_link_status_t& status, int64_t received_us, int64_t now_us);
void DrawOnboarding(raster::Canvas& canvas, const agent_link_status_t& status,
                    Language language, const char* hotspot, uint32_t remaining);
}
