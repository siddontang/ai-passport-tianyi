#pragma once

#include <atomic>
#include <cstdint>

namespace agentlink::realtime {

// Tag recovery with its request generation. Cancellation/new requests advance
// the generation; a late completion must not clear a newer call's recovery.
class RecoveryEpoch {
public:
    void Begin(uint32_t epoch) { epoch_.store(epoch); }
    void End(uint32_t epoch) { epoch_.compare_exchange_strong(epoch, 0); }
    bool Active(uint32_t current) const {
        const uint32_t epoch = epoch_.load();
        return epoch != 0 && epoch == current;
    }
private:
    std::atomic<uint32_t> epoch_{0};
};

// Each attempt owns a fresh grant, transport and audio queues. The wait callback
// must observe cancellation and return false when the user leaves the call.
template <typename Attempt, typename Wait>
void RecoverTransport(Attempt attempt, Wait wait) {
    constexpr unsigned max_retries = 2;
    for (unsigned retries = 0;; ++retries) {
        if (!attempt(retries < max_retries) || retries == max_retries) return;
        if (!wait(retries + 1, 2000U << retries)) return;
    }
}

}  // namespace agentlink::realtime
