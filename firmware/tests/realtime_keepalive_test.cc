// Exercise the production C3 keepalive branch with a delayed receive path.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "realtime_ws_frame.h"

#define ESP_LOGI(...) ((void)0)
struct Pending { bool busy = false; bool Busy() const { return busy; } };
struct Sender { Pending pending; uint8_t wire[256]{}; size_t capacity = sizeof wire; } sender;
struct Frame { int64_t ms; unsigned opcode; };
std::vector<Frame> frames;
int64_t last_keepalive = 0, now_ms = 0;
unsigned peer_pings = 0, keepalives = 0;
bool retryable = false, fail_write = false;
void esp_fill_random(void* data, size_t bytes) { memset(data, 0xa5, bytes); }
bool WriteWire(Sender& value, size_t size) {
    assert(!value.pending.Busy());
    assert(size == 6 && (value.wire[0] & 0x80) && value.wire[1] == 0x80);
    if (fail_write) return false;
    frames.push_back({now_ms, unsigned(value.wire[0] & 0x0f)});
    return true;
}
void Tick(int64_t now) {
    now_ms = now / 1000;
    do {
        // INSERT_C3_KEEPALIVE
    } while (false);
}
int main() {
    // A long story delays every inbound Ping; outgoing audio occupies the
    // writer on alternating ticks. Keepalive must still make progress.
    for (int64_t ms = 0; ms <= 180000; ms += 10) {
        sender.pending.busy = ms % 40 < 20;
        Tick(ms * 1000);
    }
    assert(!retryable && frames.size() >= 17);
    int64_t previous = 0;
    for (const Frame& frame : frames) {
        assert(frame.opcode == 10);
        assert(frame.ms - previous >= 10000 && frame.ms - previous <= 10020);
        previous = frame.ms;
    }
    // The Python runner can replay these actual production-generated frames
    // through the gateway's unmodified heartbeat and Pong handler bodies.
    printf("[");
    for (size_t i = 0; i < frames.size(); ++i)
        printf("%s[%lld,%u]", i ? "," : "", static_cast<long long>(frames[i].ms), frames[i].opcode);
    puts("]");

    const auto count = frames.size();
    const auto previous_deadline = last_keepalive;
    sender.pending.busy = true;
    Tick(200000000); // Never overwrite an in-flight microphone/TLS frame.
    assert(frames.size() == count && last_keepalive == previous_deadline);
    sender.pending.busy = false; fail_write = true;
    Tick(200000000);
    assert(retryable && last_keepalive == previous_deadline);
}
