#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "realtime_captions.h"
using agentlink::realtime::Captions;
static std::string Repeat(const char* word, unsigned n) {
    std::string result;
    while (n--) result += word;
    return result;
}
int main() {
    Captions captions;
    char shown[384]{};
    const std::string story = Repeat("天依的小故事。", 100); // 700 Chinese/punctuation characters.
    captions.Begin("story");
    assert(captions.Text(story.c_str(), "story", 0, true));
    assert(captions.Render(0, shown, sizeof shown) && !shown[0]);
    assert(captions.Render(16000, shown, sizeof shown));
    assert(std::strlen(shown) > 0 && std::strlen(shown) <= 18);
    const std::string first = shown;
    // A network stall does not spend wall-clock time as subtitle progress.
    for (unsigned i = 0; i < 600; ++i) assert(!captions.Render(16000, shown, sizeof shown));
    assert(first == shown);
    // A cumulative/final transcript delivered early never dumps its tail.
    assert(captions.Text(story.c_str(), "story", 16000, true));
    captions.Render(16000, shown, sizeof shown);
    assert(first == shown);
    assert(!captions.Text("WRONG RESPONSE", "old", 16000, true));
    for (uint32_t ms = 1100; ms <= 150000; ms += 100) {
        captions.Text(story.c_str(), "story", ms * 16, true);
        captions.Render(ms * 16, shown, sizeof shown);
        assert((static_cast<unsigned char>(shown[0]) & 0xc0) != 0x80);
    }
    assert(!captions.Overflow() && captions.VisibleUnits() > 500000);
    captions.End(160000 * 16);
    captions.Render(160000 * 16 - 1, shown, sizeof shown);
    assert(captions.VisibleUnits() < captions.TotalUnits());
    captions.Render(160000 * 16, shown, sizeof shown);
    assert(captions.VisibleUnits() == captions.TotalUnits());

    // Playback cursor waits for software queue + bounded DMA tail, and freezes.
    captions.Begin("clock");
    assert(captions.Played(16000, 32000, 0) == 0);
    assert(captions.Played(16000, 32000, 60000000) == 0);
    assert(captions.Played(16000, 0, 60000000) == 14560);
    assert(captions.Played(16000, 0, 60045000) == 15280);
    assert(captions.Played(16000, 0, 60090000) == 16000);
    assert(captions.Played(16000, 0, 180000000) == 16000);

    // Input remains immediate in the Call integration. Suppress old assistant
    // output, preserve pre-start text of the next reply, and clear on interrupt.
    captions.User();
    assert(!captions.Render(200000, shown, sizeof shown));
    assert(!captions.Text("OLD", "clock", 16000, true));
    assert(captions.Text("你好，天依。", "next", 0, false));
    assert(!captions.Render(16000, shown, sizeof shown));
    captions.Begin("next");
    captions.Render(0, shown, sizeof shown); assert(!shown[0]);
    captions.Render(16000, shown, sizeof shown); assert(shown[0]);
    captions.Cancel(); assert(!captions.Render(999999, shown, sizeof shown));

    // UTF-8/UTF-16 boundaries, malformed input and finite memory for huge input.
    captions.Begin("unicode");
    captions.Text("洛天依🎵你好 English!", "unicode", 0, true);
    captions.End(16000); captions.Render(16000, shown, sizeof shown);
    assert(std::strcmp(shown, "洛天依🎵你好 English!") == 0);
    captions.Begin("large");
    const std::string large = Repeat("天", 20000);
    captions.Text(large.c_str(), "large", 0, true); assert(captions.Overflow());
    captions.Render(16000, shown, sizeof shown); assert(std::strlen(shown) == 12);
    for (unsigned n = 1; n <= 4; ++n) {
        captions.Begin("bad");
        const char bad[] = {char(0xf0), char(0x9f), char(0x8e), char(0xb5), 0};
        captions.Text(std::string(bad, n).c_str(), "bad", 0, true);
        captions.End(16000); captions.Render(16000, shown, sizeof shown);
    }
    std::printf("PASS: playback-gated captions; early final, 150-second story, stall, drain, roles, interruption, UTF-8, bounded storage (%zu bytes)\n", sizeof(Captions));
}
