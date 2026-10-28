// Onboarding adapted from deota-jonah/ai-passport eaee36b (MIT).
#include "passport_onboarding.h"
#include <cstdio>
#include <cstring>

extern "C" const raster_font_t passport_font_16;
namespace passport_wifi {
namespace {
void Text(raster::Canvas& canvas, const raster_font_t& font, const char* text,
          int x, int y, unsigned lines, uint32_t color = 0xC7D3D9) {
    raster::TextLayout layout;
    layout.Wrap(font, text, 192);
    for (unsigned i = 0; i < layout.Count() && i < lines; ++i)
        canvas.Text(font, x, y + i * (font.line_height + 3), text + layout.Start(i), text + layout.End(i), color);
}
bool ValidCode(const char* code) {
    if (std::strlen(code) != 6) return false;
    for (unsigned i = 0; i < 6; ++i) if (code[i] < '0' || code[i] > '9') return false;
    return true;
}
}
uint32_t CodeRemaining(const agent_link_status_t& status, int64_t received_us, int64_t now_us) {
    const uint64_t elapsed = now_us > received_us ? (now_us - received_us) / 1000000 : 0;
    return elapsed >= status.expires_s ? 0 : status.expires_s - elapsed;
}
void DrawOnboarding(raster::Canvas& canvas, const agent_link_status_t& status,
                    Language language, const char* hotspot, uint32_t remaining) {
    auto tr = [language](TextId id) { return Translate(id, language); };
    const auto& font = language == Language::Chinese ? passport_font_16 : raster_montserrat_14;
    constexpr uint32_t accent = 0x8AE1D8;
    canvas.Fill(0x101820);
    canvas.Box({12, 40, 216, 234}, 0x1D2B36, 12);
    Text(canvas, font, tr(TextId::Header), 16, 12, 1, accent);
    canvas.Clip({24, 52, 192, 220});
    if (status.phase == AGENT_LINK_PHASE_SETUP) {
        TextId title = TextId::SetupTitle;
        if (strstr(status.hint, "Authentication failed")) title = TextId::SetupBadPassword;
        else if (strstr(status.hint, "Network not found")) title = TextId::SetupNotFound;
        else if (strstr(status.hint, "timed out") || strstr(status.hint, "No IP")) title = TextId::SetupFailed;
        Text(canvas, font, tr(title), 24, 54, 2, accent);
        Text(canvas, font, tr(TextId::SetupPhone), 24, 101, 1);
        Text(canvas, raster_montserrat_14, hotspot, 24, 126, 2, 0xF4F4EC);
        Text(canvas, font, tr(TextId::SetupBrowser), 24, 170, 1);
        Text(canvas, raster_montserrat_20, "192.168.4.1", 24, 195, 1, accent);
        Text(canvas, font, tr(TextId::SetupCredentials), 24, 229, 2);
    } else if (status.phase == AGENT_LINK_PHASE_PAIRING) {
        Text(canvas, font, tr(TextId::CodeTitle), 24, 54, 1, accent);
        const bool valid = remaining && ValidCode(status.code);
        Text(canvas, raster_montserrat_28, valid ? status.code : "------", 24, 85, 1, 0xF4F4EC);
        Text(canvas, font, tr(TextId::CodeInstructions), 24, 130, 2);
        Text(canvas, raster_montserrat_14, "bot.deotaland.cn\n/#/devices", 24, 185, 2, accent);
        char expiry[64];
        if (valid) snprintf(expiry, sizeof expiry, tr(TextId::CodeRemaining), static_cast<unsigned long>(remaining));
        else raster::CopyText(expiry, sizeof expiry, tr(TextId::CodeRefreshing));
        Text(canvas, font, expiry, 24, 239, 1);
    } else {
        TextId title = TextId::Connecting, hint = TextId::ConnectingHint;
        if (strcmp(status.title, "Joining WiFi") == 0) { title = TextId::Joining; hint = TextId::JoiningHint; }
        else if (strcmp(status.title, "Signing in") == 0) { title = TextId::SigningIn; hint = TextId::SigningInHint; }
        else if (strcmp(status.title, "Reconnecting") == 0) { title = TextId::Retrying; hint = TextId::RetryingHint; }
        else if (strcmp(status.title, "Already bound") == 0) { title = TextId::AlreadyBound; hint = TextId::AlreadyBoundHint; }
        else if (strcmp(status.title, "No agent") == 0) { title = TextId::NoAgent; hint = TextId::NoAgentHint; }
        else if (status.phase == AGENT_LINK_PHASE_BLOCKED) { title = TextId::PlatformError; hint = TextId::PlatformErrorHint; }
        Text(canvas, font, tr(title), 24, 54, 2, accent);
        Text(canvas, font, tr(hint), 24, 112, 5);
        Text(canvas, raster_montserrat_14, "bot.deotaland.cn\n/#/devices", 24, 233, 2, accent);
    }
    canvas.Clip({12, 276, 216, 44});
    Text(canvas, font, tr(TextId::SetupFooter), 14, 276, 2);
}
}
