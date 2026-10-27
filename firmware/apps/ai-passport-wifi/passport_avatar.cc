#include "passport_avatar.h"
#include "stripe_canvas.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" const raster_font_t passport_font_16;

namespace passport_wifi::avatar {
namespace {
constexpr uint32_t kBackground = 0x0B2028;
constexpr uint32_t kMint = 0x8AE1D8;

bool Inside(const Patch& patch, int x, int y) {
    return x >= patch.x && x < patch.x + patch.width &&
           y >= patch.y && y < patch.y + patch.height;
}
bool Overlaps(int y, int top, int bottom) {
    return y < bottom && y + kStripeRows > top;
}
Face Mouth(Face face) {
    return face == Face::TalkSmall || face == Face::TalkOpen ? face : Face::Smile;
}

void Text(raster::Canvas& canvas, const raster_font_t& font, const char* text,
          int x, int y, int width, uint32_t color) {
    if (!text || !*text) return;
    raster::TextLayout layout;
    layout.Wrap(font, text, width);
    if (layout.Count())
        canvas.Text(font, x, y, text + layout.Start(0), text + layout.End(0), color);
}
}

void CopySubtitle(char* destination, size_t capacity, const char* text) {
    if (!destination || !capacity) return;
    if (!text) text = "";
    const size_t bytes = std::strlen(text);
    size_t offset = bytes >= capacity ? bytes - capacity + 1 : 0;
    while (offset < bytes && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) ++offset;
    std::memmove(destination, text + offset, bytes - offset + 1);
}

void SubtitleLayout::Wrap(const char* text) {
    raster::TextLayout layout;
    layout.Wrap(passport_font_16, text, 198);
    count = std::min(2u, layout.Count());
    for (unsigned i = 0; i < count; ++i) {
        const unsigned line = layout.Count() - count + i;
        start[i] = layout.Start(line); end[i] = layout.End(line);
    }
}

const char* Caption(State state) {
    switch (state) {
    case State::Listening: return "Listening to you";
    case State::Speaking: return "Talking with you";
    case State::Waiting: return "Starting voice";
    default: return "Here with you";
    }
}

bool NeedsStripe(int y, bool full, const Pose& previous, const Pose& next) {
    if (full) return true;
    if ((previous.face == Face::Blink) != (next.face == Face::Blink) &&
        Overlaps(y, kPortraitY + kEyes.y * kScale,
                 kPortraitY + (kEyes.y + kEyes.height) * kScale))
        return true;
    if (Mouth(previous.face) != Mouth(next.face) &&
        Overlaps(y, kPortraitY + kMouth.y * kScale,
                 kPortraitY + (kMouth.y + kMouth.height) * kScale))
        return true;
    return (previous.pulse != next.pulse || previous.meter != next.meter) &&
           Overlaps(y, 276, 288);
}

bool NeedsContentStripe(int y) {
    return y < kPortraitY || y + kStripeRows > kPortraitBottom;
}

bool NeedsSubtitleStripe(int y) { return Overlaps(y, 236, 300); }

void RenderStripe(uint8_t* stripe, int y, State state, const Pose& pose,
                  const char* answer, const char* footer, int volume,
                  const SubtitleLayout* subtitle, const char* waiting_hint) {
    if (!stripe || y < 0 || y + kStripeRows > kScreenHeight) return;
    raster::Canvas canvas(stripe, kScreenWidth, y, kStripeRows);
    canvas.Fill(kBackground);
    Text(canvas, raster_montserrat_14, "TIANYI", 14, 5, 132, kMint);
    char volume_text[12];
    std::snprintf(volume_text, sizeof volume_text, "VOL%d", std::max(0, std::min(100, volume)));
    Text(canvas, raster_montserrat_14, volume_text, 169, 5, 58, 0xCEE8EC);
    canvas.Box({157, 10, 5, 5}, 0x6CE2BD, 2);

    for (int row = std::max(y, kPortraitY); row < std::min(y + kStripeRows, kPortraitBottom); ++row) {
        const int sy = (row - kPortraitY) / kScale;
        uint8_t* dest = stripe + ((row - y) * kScreenWidth + kPortraitX) * 2;
        for (int sx = 0; sx < kWidth; ++sx) {
            // Use a fixed base so generated-frame differences cannot move hair
            // or clothing. Only the authored eye/mouth windows are animated.
            Face face = Face::Smile;
            if (pose.face == Face::Blink && Inside(kEyes, sx, sy)) face = Face::Blink;
            else if (Inside(kMouth, sx, sy)) face = Mouth(pose.face);
            const uint8_t* pixel = kPixels[static_cast<unsigned>(face)] + (sy * kWidth + sx) * 2;
            for (int repeat = 0; repeat < kScale; ++repeat) {
                *dest++ = pixel[0]; *dest++ = pixel[1];
            }
        }
    }
    if (answer && *answer) {
        // Text has its own dirty region; the body and face remain untouched.
        if (NeedsSubtitleStripe(y)) {
            SubtitleLayout local;
            if (!subtitle) { local.Wrap(answer); subtitle = &local; }
            canvas.Box({12, 236, 216, 62}, 0x153840, 9);
            Text(canvas, raster_montserrat_14, "LIVE CAPTIONS", 22, 237, 198, kMint);
            for (unsigned i = 0; i < subtitle->count; ++i)
                canvas.Text(passport_font_16, 22, 254 + i * 20,
                            answer + subtitle->start[i], answer + subtitle->end[i], 0xF0FFFC);
        }
    } else {
    canvas.Box({12, 249, 216, 47}, 0x153840, 9);
    Text(canvas, raster_montserrat_20, Caption(state), 22, 252, 198, 0xF0FFFC);
    const char* hint = state == State::Speaking ? "" : state == State::Listening ? "I'm all ears" :
                       state == State::Waiting ? "Getting ready" : "Press OK. Let's talk.";
    if (state == State::Waiting && waiting_hint && *waiting_hint) hint = waiting_hint;
    if (state == State::Speaking && (!hint || !*hint)) {
        for (int bar = 0; bar < 9; ++bar) {
            const int height = 2 + ((pose.meter + bar * 3 + pose.pulse) % 8);
            canvas.Box({75 + bar * 10, 286 - height, 5, height}, kMint, 2);
        }
    } else {
        Text(canvas, raster_montserrat_14, hint, 22, 276, 198, kMint);
    }
    if (state == State::Listening || state == State::Waiting) {
        for (int dot = 0; dot < 3; ++dot)
            canvas.Box({201 + dot * 6, 282, 3, 3}, dot <= pose.pulse ? kMint : 0x38626A, 1);
    }
    }
    Text(canvas, raster_montserrat_14, footer, 12, 302, 216, 0xA5C5CC);
}
}  // namespace passport_wifi::avatar
