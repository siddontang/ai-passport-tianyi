#include "passport_wifi_ui.h"
#include "passport_avatar.h"
#include "stripe_canvas.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" const raster_font_t passport_font_16;

namespace passport_wifi {
namespace {
struct Frame {
    agent_link_status_t status{};
    char title[80] = "Starting";
    char detail[384] = "Connecting to Agent Link";
    char footer[80] = "OK: call   Hold OK: menu";
    char answer[384]{};
    View view = View::Text;
    int volume = 70;
    uint32_t revision = 1;
    uint32_t subtitle_revision = 1;
};
Frame input, frame;
std::mutex lock;
St7789Lcd* panel = nullptr;
avatar::Envelope envelope;

avatar::State PortraitState(const Frame& value) {
    if (strcmp(value.title, "Agent speaking") == 0) return avatar::State::Speaking;
    if (strcmp(value.title, "Listening") == 0) return avatar::State::Listening;
    if (strcmp(value.title, "Starting call") == 0 || strcmp(value.title, "Reconnecting") == 0)
        return avatar::State::Waiting;
    return avatar::State::Idle;
}

bool Latin(const char* text) {
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
        if (*p >= 0x80) return false;
    return true;
}
void Text(raster::Canvas& canvas, const raster_font_t& preferred_font, const char* text,
          int x, int y, int width, int lines, uint32_t color) {
    const auto& font = Latin(text) ? preferred_font : passport_font_16;
    raster::TextLayout layout;
    layout.Wrap(font, text, width);
    for (unsigned i = 0; i < layout.Count() && i < static_cast<unsigned>(lines); ++i)
        canvas.Text(font, x, y + i * (font.line_height + 3),
                    text + layout.Start(i), text + layout.End(i), color);
}
void Render(void*) {
    // Add one tick so integer tick conversion never exceeds the 8 fps cap.
    const TickType_t frame_delay = pdMS_TO_TICKS(avatar::kFrameIntervalMs) + 1;
    uint32_t seen = 0;
    uint32_t subtitle_seen = 0;
    avatar::SubtitleLayout subtitle;
    avatar::Pose previous{};
    bool was_portrait = false;
    uint32_t stats_at = 0, drawn_stripes = 0;
    uint32_t waiting_at = 0, waiting_second = UINT32_MAX;
    avatar::State previous_state = avatar::State::Idle;
    for (;;) {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (seen != input.revision || subtitle_seen != input.subtitle_revision) frame = input;
        }
        const bool link = frame.status.phase != AGENT_LINK_PHASE_CONNECTED &&
                          frame.status.phase != AGENT_LINK_PHASE_READY;
        const bool portrait = !link && frame.view == View::Portrait;
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        const auto state = PortraitState(frame);
        if (state == avatar::State::Waiting && previous_state != state) waiting_at = now_ms;
        const uint32_t second = state == avatar::State::Waiting ? (now_ms - waiting_at) / 1000 : UINT32_MAX;
        const bool waiting_changed = waiting_second != second;
        char waiting_hint[64]{};
        if (state == avatar::State::Waiting) {
            const char* stage = strcmp(frame.title, "Reconnecting") == 0 ? "Reconnecting" :
                                strstr(frame.status.hint, "access") ? "Requesting access" :
                                strstr(frame.status.hint, "model") ? "Starting voice model" :
                                strstr(frame.status.hint, "Connecting") ? "Opening connection" : "Preparing call";
            snprintf(waiting_hint, sizeof waiting_hint, "%s %us", stage, unsigned(second));
        }
        const auto pose = avatar::Animate(state, now_ms, envelope.Read(now_ms));
        const bool content_changed = seen != frame.revision || was_portrait != portrait || waiting_changed;
        const bool subtitle_changed = subtitle_seen != frame.subtitle_revision;
        if (subtitle_changed) subtitle.Wrap(frame.answer);
        const bool full = !portrait || !was_portrait;
        if (now_ms - stats_at >= 10000) {
            ESP_LOGI("Passport.avatar", "pixel UI: %u stripes / %u ms, %u display bytes",
                     static_cast<unsigned>(drawn_stripes), static_cast<unsigned>(now_ms - stats_at),
                     static_cast<unsigned>(drawn_stripes * 1920));
            stats_at = now_ms; drawn_stripes = 0;
        }
        if (!content_changed && !subtitle_changed && (!portrait || pose == previous)) {
            vTaskDelay(frame_delay); continue;
        }
        const bool pairing = frame.status.phase == AGENT_LINK_PHASE_PAIRING && frame.status.code[0];
        const char* title = pairing ? frame.status.code : link ? frame.status.title : frame.title;
        const char* detail = link ? frame.status.hint : frame.detail;
        const uint32_t accent = link ? 0xFFB54A : 0x54D6B4;
        for (int y = 0; y < 320; y += 4) {
            if (portrait && !avatar::NeedsStripe(y, full, previous, pose) &&
                !(content_changed && avatar::NeedsContentStripe(y)) &&
                !(subtitle_changed && avatar::NeedsSubtitleStripe(y))) continue;
            if (portrait) {
                avatar::RenderStripe(panel->StripeBuffer(), y, state, pose, frame.answer, frame.footer, frame.volume, &subtitle, waiting_hint);
            } else {
            raster::Canvas canvas(panel->StripeBuffer(), 240, y, 4);
            canvas.Fill(0x101820);
            canvas.Box({12, 40, 216, 236}, 0x1D2B36, 12);
            Text(canvas, raster_montserrat_14, "AI PASSPORT / WIFI", 16, 12, 212, 1, accent);
            canvas.Clip({24, 52, 192, 212});
            Text(canvas, pairing ? raster_montserrat_28 : raster_montserrat_20,
                 title, 24, 54, 192, 2, 0xF4F4EC);
            Text(canvas, Latin(title) ? raster_montserrat_14 : passport_font_16,
                 detail, 24, 111, 192, 6, 0xC7D3D9);
            if (!link && frame.answer[0])
                Text(canvas, raster_montserrat_14, frame.answer, 24, 213, 192, 2, accent);
            canvas.Clip({12, 281, 216, 39});
            Text(canvas, raster_montserrat_14, link ? "UP / DOWN: volume" : frame.footer,
                 14, 284, 212, 2, 0xC7D3D9);
            }
            if (panel->FlushStripe(y, 4) != ESP_OK) {
                // A timed-out DMA may still own the stripe. Stop rendering rather
                // than overwrite that buffer; audio/network workers keep running.
                ESP_LOGE("Passport.avatar", "Display transfer failed; animation stopped safely");
                vTaskDelete(nullptr);
                return;
            }
            ++drawn_stripes;
            if (y % 32 == 28) vTaskDelay(1);
        }
        seen = frame.revision;
        subtitle_seen = frame.subtitle_revision;
        previous = pose;
        previous_state = state; waiting_second = second;
        if (portrait && !was_portrait)
            ESP_LOGI("Passport.avatar", "Tianyi pixel portrait active: 64x64 sprites, face patches, 8 fps cap, 1920-byte stripe");
        was_portrait = portrait;
        vTaskDelay(frame_delay);
    }
}
}  // namespace

esp_err_t StartUi(St7789Lcd* lcd) {
    if (!lcd || !lcd->Ready()) return ESP_ERR_INVALID_STATE;
    panel = lcd;
    return xTaskCreate(Render, "passport_ui", 4096, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
void SetStatus(const agent_link_status_t& status) {
    std::lock_guard<std::mutex> guard(lock);
    if (input.status.phase == status.phase && strcmp(input.status.title, status.title) == 0 &&
        strcmp(input.status.hint, status.hint) == 0 && strcmp(input.status.code, status.code) == 0) return;
    input.status = status; ++input.revision;
}
void SetContent(const char* title, const char* detail, const char* footer, View view, int volume) {
    std::lock_guard<std::mutex> guard(lock);
    if (strcmp(input.title, title) == 0 && strcmp(input.detail, detail) == 0 &&
        strcmp(input.footer, footer) == 0 && input.view == view &&
        (volume < 0 || input.volume == volume)) return;
    raster::CopyText(input.title, sizeof input.title, title);
    raster::CopyText(input.detail, sizeof input.detail, detail);
    raster::CopyText(input.footer, sizeof input.footer, footer);
    input.view = view;
    if (volume >= 0) input.volume = volume > 100 ? 100 : volume;
    ++input.revision;
}
void SetAnswer(const char* text) {
    if (!text) return;
    std::lock_guard<std::mutex> guard(lock);
    if (strcmp(input.answer, text) == 0) return;
    avatar::CopySubtitle(input.answer, sizeof input.answer, text);
    ++input.subtitle_revision;
}
void SetPlaybackLevel(uint8_t level) {
    envelope.Observe(level, static_cast<uint32_t>(esp_timer_get_time() / 1000));
}
void ClearPlaybackLevel() { envelope.Clear(); }
}  // namespace passport_wifi
