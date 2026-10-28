#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "passport_language.h"

namespace passport_wifi::avatar {

constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr int kFrames = 4;
constexpr int kScale = 3;
constexpr int kPortraitX = 24;
constexpr int kPortraitY = 40;
constexpr int kPortraitBottom = kPortraitY + kHeight * kScale;
constexpr int kFrameIntervalMs = 125;
struct Patch { int x, y, width, height; };
constexpr Patch kEyes{20, 29, 29, 11};
constexpr Patch kMouth{29, 39, 12, 7};
constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;
constexpr int kStripeRows = 4;
enum class Face : uint8_t { Smile, Blink, TalkSmall, TalkOpen };
enum class State : uint8_t { Idle, Listening, Speaking, Waiting };

// Keep the newest complete UTF-8 characters, including long partial transcripts.
void CopySubtitle(char* destination, size_t capacity, const char* text);
struct SubtitleLayout {
    uint16_t start[2]{}, end[2]{};
    unsigned count = 0;
    void Wrap(const char* text);
};

// Four immutable RGB565 frames live in mapped flash, never in the C3 heap.
extern const uint8_t kPixels[kFrames][kWidth * kHeight * 2];

inline uint8_t PcmLevel(const int16_t* pcm, size_t samples) {
    if (!pcm || !samples) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < samples; ++i) {
        const int32_t sample = pcm[i];
        sum += sample < 0 ? -sample : sample;
    }
    const uint32_t average = static_cast<uint32_t>(sum / samples);
    const uint32_t level = average * 255 / 6000;
    return static_cast<uint8_t>(level > 255 ? 255 : level);
}

// One atomic word avoids UI locks/allocations in the audio worker. Time wraps
// every 2^24 ms; modular subtraction keeps the short expiry valid across wrap.
class Envelope {
public:
    void Observe(uint8_t level, uint32_t now_ms) {
        const uint32_t old = value_.load(std::memory_order_relaxed);
        const uint32_t age = ((now_ms & kTimeMask) - (old >> 8)) & kTimeMask;
        const uint8_t previous = static_cast<uint8_t>(old);
        if (age < 45 && previous > level)
            level = static_cast<uint8_t>((previous * 2 + level) / 3);
        value_.store(((now_ms & kTimeMask) << 8) | level, std::memory_order_relaxed);
    }
    uint8_t Read(uint32_t now_ms) const {
        const uint32_t value = value_.load(std::memory_order_relaxed);
        const uint32_t age = ((now_ms & kTimeMask) - (value >> 8)) & kTimeMask;
        if (age >= 180) return 0;
        const uint32_t level = static_cast<uint8_t>(value);
        return static_cast<uint8_t>(age < 60 ? level : level * (180 - age) / 120);
    }
    void Clear() { value_.store(0, std::memory_order_relaxed); }
private:
    static constexpr uint32_t kTimeMask = 0xFFFFFF;
    std::atomic<uint32_t> value_{0};
};

struct Pose {
    Face face;
    int8_t offset_y;
    uint8_t pulse;
    uint8_t meter;
    bool operator==(const Pose& other) const {
        return face == other.face && offset_y == other.offset_y &&
               pulse == other.pulse && meter == other.meter;
    }
    bool operator!=(const Pose& other) const { return !(*this == other); }
};

inline Pose Animate(State state, uint32_t now_ms, uint8_t level) {
    Face face = now_ms % 4800 >= 4200 && now_ms % 4800 < 4400 ? Face::Blink : Face::Smile;
    if (state == State::Speaking && face != Face::Blink && level >= 24) {
        face = level < 110 ? Face::TalkSmall : Face::TalkOpen;
    }
    const uint8_t pulse = state == State::Idle ? 0 : static_cast<uint8_t>((now_ms / 400) % 4);
    // Keep the body fixed: every blink/mouth change transfers only its face rows.
    return {face, 0, pulse,
            static_cast<uint8_t>(state == State::Speaking ? level / 64 : 0)};
}

// The caller owns exactly one four-row DMA stripe. No framebuffer is needed.
void RenderStripe(uint8_t* stripe, int y, State state, const Pose& pose,
                  const char* answer, const char* footer, int volume = 70,
                  const SubtitleLayout* subtitle = nullptr, const char* waiting_hint = nullptr,
                  Language language = Language::English, int battery = -1);
bool NeedsStripe(int y, bool full, const Pose& previous, const Pose& next);
bool NeedsContentStripe(int y);
bool NeedsSubtitleStripe(int y);
const char* Caption(State state, Language language = Language::English);

}  // namespace passport_wifi::avatar
