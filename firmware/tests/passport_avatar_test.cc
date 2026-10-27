#include "passport_avatar.h"
#include "stripe_canvas.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" const raster_font_t passport_font_16;

using namespace passport_wifi::avatar;

namespace {
void WritePreview(const char* directory, const char* name, State state, uint32_t now, uint8_t level,
                  const char* subtitle = "") {
    const auto pose = Animate(state, now, level);
    const std::string path = std::string(directory) + "/" + name + ".ppm";
    FILE* file = std::fopen(path.c_str(), "wb"); assert(file);
    std::fprintf(file, "P6\n240 320\n255\n");
    std::array<uint8_t, 240 * 4 * 2> stripe{};
    for (int y = 0; y < 320; y += 4) {
        RenderStripe(stripe.data(), y, state, pose, subtitle, state == State::Idle ? "OK: call   Hold: menu" : "OK: end   Hold: menu");
        for (size_t i = 0; i < stripe.size(); i += 2) {
            const uint16_t pixel = (stripe[i] << 8) | stripe[i + 1];
            const uint8_t rgb[] = {
                static_cast<uint8_t>((pixel >> 11) * 255 / 31),
                static_cast<uint8_t>(((pixel >> 5) & 63) * 255 / 63),
                static_cast<uint8_t>((pixel & 31) * 255 / 31)};
            assert(std::fwrite(rgb, 1, 3, file) == 3);
        }
    }
    std::fclose(file);
}
}

int main(int argc, char** argv) {
    char tail[17];
    CopySubtitle(tail, sizeof tail, "你好，天依，今天心情怎么样？");
    assert(std::strcmp(tail, "情怎么样？") == 0);
    CopySubtitle(tail, sizeof tail, "你好，hello");
    assert(std::strcmp(tail, "你好，hello") == 0);
    CopySubtitle(tail, sizeof tail, ""); assert(!tail[0]);
    const char* chinese = "你好，天依，请介绍一下你自己。";
    const char* cursor = chinese;
    const char* end = chinese + std::strlen(chinese);
    while (cursor < end) assert(raster::GlyphId(passport_font_16, raster::NextCodepoint(cursor, end)));
    SubtitleLayout layout;
    layout.Wrap(chinese);
    assert(layout.count == 2 && layout.end[1] == std::strlen(chinese));
    for (unsigned i = 0; i < layout.count; ++i)
        assert(raster::TextWidth(passport_font_16, chinese + layout.start[i], chinese + layout.end[i]) <= 198);
    const int16_t silence[] = {0, 0, 0};
    const int16_t voice[] = {3000, -3000};
    const int16_t limits[] = {-32768, 32767};
    assert(PcmLevel(nullptr, 10) == 0);
    assert(PcmLevel(voice, 0) == 0);
    assert(PcmLevel(silence, 3) == 0);
    assert(PcmLevel(voice, 2) == 127);
    assert(PcmLevel(limits, 2) == 255);

    Envelope envelope;
    assert(envelope.Read(0) == 0);
    envelope.Observe(240, 1000);
    assert(envelope.Read(1010) == 240);
    assert(envelope.Read(1120) == 120);
    assert(envelope.Read(1180) == 0);
    envelope.Observe(255, 2000);
    for (uint32_t now = 2016; now < 2160; now += 16) envelope.Observe(0, now);
    assert(envelope.Read(2160) < 24); // repeated silence must not hold a stale peak
    envelope.Observe(200, 0xFFFFF0);
    assert(envelope.Read(0x1000010) == 200);
    assert(envelope.Read(0x10000B0) == 0);
    envelope.Clear(); assert(envelope.Read(0) == 0);

    assert(Animate(State::Idle, 1000, 255).face == Face::Smile);
    assert(Animate(State::Listening, 4250, 255).face == Face::Blink);
    assert(Animate(State::Speaking, 1000, 0).face == Face::Smile);
    assert(Animate(State::Speaking, 1000, 60).face == Face::TalkSmall);
    assert(Animate(State::Speaking, 1000, 240).face == Face::TalkOpen);
    assert(Animate(State::Waiting, 1000, 255).meter == 0);

    static_assert(sizeof kPixels == 32768, "Pixel portraits must fit the 32 KB flash budget");
    const Pose still{Face::Smile, 0, 0, 0}, talking{Face::TalkOpen, 0, 2, 3};
    unsigned mouth_stripes = 0, blink_stripes = 0;
    for (int y = 0; y < 320; y += 4) {
        assert(NeedsStripe(y, true, still, still));
        assert(!NeedsStripe(y, false, still, still));
        assert(NeedsStripe(y, false, still, talking) ==
               ((y >= 156 && y < 180) || (y >= 276 && y < 288)));
        mouth_stripes += NeedsStripe(y, false, still, {Face::TalkOpen, 0, 0, 0});
        blink_stripes += NeedsStripe(y, false, still, {Face::Blink, 0, 0, 0});
    }
    assert(mouth_stripes == 6 && blink_stripes == 9);
    std::printf("Display budget: mouth %u bytes, blink %u bytes; previous whole-portrait change 107520 bytes\n",
                mouth_stripes * 1920, blink_stripes * 1920);
    // Simulate DMA ownership with sentinels: every face/state/breath phase must
    // stay inside the 1,920-byte stripe, including first/last rows and invalid y.
    std::array<uint8_t, 1920 + 32> guarded;
    for (int state = 0; state < 4; ++state) for (uint32_t now = 0; now < 5000; now += 77) {
        const auto pose = Animate(static_cast<State>(state), now, now % 256);
        assert(pose.offset_y == 0);
        for (int y = 0; y < 320; y += 4) {
            guarded.fill(0xA5);
            RenderStripe(guarded.data() + 16, y, static_cast<State>(state), pose,
                         "A short reply", "OK: end   Hold: menu");
            assert(std::all_of(guarded.begin(), guarded.begin() + 16, [](uint8_t x) { return x == 0xA5; }));
            assert(std::all_of(guarded.end() - 16, guarded.end(), [](uint8_t x) { return x == 0xA5; }));
        }
    }
    for (const auto state : {State::Idle, State::Listening, State::Speaking, State::Waiting}) {
        const char* caption = Caption(state);
        assert(raster::TextWidth(raster_montserrat_20, caption, caption + std::strlen(caption)) <= 198);
    }
    // Face-patch and text-only transfers must exactly match a full redraw,
    // including transitions out of speaking, without repainting the body.
    std::array<uint8_t, 240 * 320 * 2> display{}, reference{};
    std::array<uint8_t, 1920> stripe{};
    Pose prior{};
    State prior_state = State::Idle;
    for (unsigned i = 0; i < 80; ++i) {
        const auto state = static_cast<State>((i / 20) % 4);
        const auto pose = Animate(state, 3800 + i * 120, (i * 37) % 256);
        for (int y = 0; y < 320; y += 4) {
            RenderStripe(stripe.data(), y, state, pose, "", "OK: call   Hold: menu");
            std::memcpy(reference.data() + y * 240 * 2, stripe.data(), stripe.size());
            if (NeedsStripe(y, i == 0, prior, pose) ||
                (state != prior_state && NeedsContentStripe(y)))
                std::memcpy(display.data() + y * 240 * 2, stripe.data(), stripe.size());
        }
        assert(display == reference);
        prior = pose; prior_state = state;
    }
    // A changed header/footer never requires the portrait's screen rows.
    for (int y = kPortraitY; y < kPortraitBottom; y += kStripeRows)
        assert(!NeedsContentStripe(y));
    // Successive recognition snapshots, scrolling tails, and clearing replace
    // only the caption card. Its partial refresh must equal a full frame.
    unsigned updates = 0;
    for (const char* text : {"", "你好", chinese,
                            "你好，天依，请介绍一下你自己。今天我们聊一聊音乐，好吗？", ""}) {
        SubtitleLayout subtitle; subtitle.Wrap(text);
        const auto pose = Animate(State::Listening, 1000, 0);
        for (int y = 0; y < 320; y += 4) {
            RenderStripe(stripe.data(), y, State::Listening, pose, text, "OK: end", 70, &subtitle);
            std::memcpy(reference.data() + y * 480, stripe.data(), stripe.size());
            if (!updates || NeedsSubtitleStripe(y))
                std::memcpy(display.data() + y * 480, stripe.data(), stripe.size());
            if (NeedsSubtitleStripe(y)) assert(y >= kPortraitBottom && y < 300);
        }
        assert(display == reference);
        ++updates;
    }
    if (argc > 1) {
        WritePreview(argv[1], "idle", State::Idle, 1000, 0);
        WritePreview(argv[1], "blink", State::Idle, 4250, 0);
        WritePreview(argv[1], "listening", State::Listening, 4500, 0);
        WritePreview(argv[1], "talk-small", State::Speaking, 1000, 60);
        WritePreview(argv[1], "talk-open", State::Speaking, 1000, 240);
        WritePreview(argv[1], "waiting", State::Waiting, 1400, 0);
        WritePreview(argv[1], "chinese-listening", State::Listening, 1000, 0, chinese);
        WritePreview(argv[1], "chinese-speaking", State::Speaking, 1000, 240, "你好呀！我是天依，很高兴认识你。");
        if (argc > 2 && std::strcmp(argv[2], "--preview") == 0) {
            static constexpr uint8_t syllables[] = {0, 70, 160, 245, 100, 45, 200, 0};
            for (unsigned i = 0; i < 80; ++i) {
                const auto state = i < 16 || i >= 64 ? State::Idle : i < 32 ? State::Listening : State::Speaking;
                char name[32]; std::snprintf(name, sizeof name, "frame-%03u", i);
                WritePreview(argv[1], name, state, 3300 + i * 160, syllables[i % 8]);
            }
        }
    }
    std::puts("Avatar: PCM/silence/wrap, animation states, dirty stripes, layout and stripe bounds PASS");
}
