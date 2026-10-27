#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace agentlink::realtime {
// Playback-progress captions for a protocol without word timestamps. No heap
// allocation, audio buffering, sleeping, or network writes happen here.
class Captions {
public:
    static constexpr size_t kCapacity = 1024; // UTF-16: 1024 Chinese characters in 2 KiB.
    void Reset() { *this = Captions{}; }
    void Begin(const char* id) {
        // A provider may send the first transcript before response.started.
        const bool keep = !active_ && !suppressed_ && length_ &&
                          (!id_[0] || !id || !*id || std::strcmp(id_, id) == 0);
        if (!keep) ClearText();
        CopyId(id); active_ = true; suppressed_ = false;
        ended_ = false; end_samples_ = 0; last_played_ = budget_ = 0;
        rate_ = 4500; last_anchor_samples_ = last_anchor_units_ = 0;
        released_ = heard_ = 0; released_at_ = 0; dirty_ = true;
    }
    void User() { Cancel(); }
    void Cancel() { ClearText(); active_ = false; suppressed_ = true; dirty_ = true; }
    bool Text(const char* utf8, const char* id, uint32_t decoded, bool final) {
        if (!utf8 || (active_ && id && *id && id_[0] && std::strcmp(id_, id))) return false;
        if (suppressed_) {
            if (id && *id && id_[0] && std::strcmp(id_, id) == 0) return false;
            ClearText(); suppressed_ = false;
        }
        if (!active_) CopyId(id);
        // Cumulative transcripts replace the unshown window; preserve the
        // current reading position instead of copying the newest tail.
        size_t absolute = 0, stored = 0; uint32_t total = 0;
        const auto* p = reinterpret_cast<const uint8_t*>(utf8);
        while (*p) {
            const uint32_t cp = Decode(p);
            total += Weight(cp);
            uint16_t units[2]; size_t n = 1;
            if (cp > 0xffff) {
                units[0] = 0xd800 + ((cp - 0x10000) >> 10);
                units[1] = 0xdc00 + ((cp - 0x10000) & 1023); n = 2;
            } else units[0] = static_cast<uint16_t>(cp);
            for (size_t i = 0; i < n; ++i, ++absolute)
                if (absolute >= base_ && stored < kCapacity) text_[stored++] = units[i];
        }
        if (absolute < base_) { ClearText(); return Text(utf8, id, decoded, final); }
        length_ = stored;
        // Never retain half a surrogate pair at the bounded window edge.
        if (length_ && High(text_[length_ - 1])) --length_;
        visible_ = std::min(visible_, length_);
        total_units_ = total; overflow_ = absolute > base_ + length_;
        // Refine only from plausible incremental audio/text anchors. A burst
        // of advance text or a duplicate final must not accelerate captions.
        if (!final && decoded >= last_anchor_samples_ + 16000 && total > last_anchor_units_) {
            const uint64_t estimate = uint64_t(total - last_anchor_units_) * 16000 /
                                      (decoded - last_anchor_samples_);
            if (estimate >= 3000 && estimate <= 6500) rate_ = (rate_ * 3 + estimate) / 4;
            last_anchor_samples_ = decoded; last_anchor_units_ = total;
        }
        dirty_ = true; return true;
    }
    void End(uint32_t samples) { ended_ = true; end_samples_ = samples; }

    // Queue release is measured, not inferred from time since the reply began.
    // The ES8311's six 240-frame DMA buffers add at most 90 ms after release.
    // Drain that bounded tail, then freeze even if a network stall lasts minutes.
    uint32_t Played(uint32_t submitted, size_t queued_bytes, int64_t now_us) {
        const uint32_t released = submitted - std::min<uint32_t>(submitted, queued_bytes / 2);
        if (released != released_) { released_ = released; released_at_ = now_us; }
        const uint32_t elapsed = static_cast<uint32_t>(std::max<int64_t>(0, std::min<int64_t>(90000, now_us - released_at_)) * 16 / 1000);
        const uint32_t pending = 1440 - elapsed;
        heard_ = std::max(heard_, released - std::min(released, pending));
        return heard_;
    }
    bool Render(uint32_t played, char* out, size_t capacity) {
        if (!active_ || suppressed_ || !capacity) return false;
        if (played > last_played_) {
            budget_ += uint64_t(played - last_played_) * rate_;
            last_played_ = played;
        }
        const uint64_t available = budget_ / 16000;
        size_t next = visible_; uint32_t used = shown_units_;
        const bool drained = ended_ && end_samples_ && played >= end_samples_;
        while (next < length_) {
            size_t end = next; const uint32_t cp = Codepoint(end);
            const uint32_t weight = Weight(cp);
            // The received text may be the whole story while its audio is
            // still streaming. Do not mark its final character heard early.
            if (!drained && end == length_ && !overflow_) break;
            if (!drained && uint64_t(used) + weight > available) break;
            used += weight; next = end;
        }
        if (!dirty_ && next == visible_) return false;
        dirty_ = false; visible_ = next; shown_units_ = used;
        // Keep only the visible suffix that fits the UI, at UTF-8 boundaries.
        size_t start = visible_, bytes = 0;
        while (start) {
            size_t prev = start - 1;
            if (Low(text_[prev]) && prev && High(text_[prev - 1])) --prev;
            size_t end = prev; const uint32_t cp = Codepoint(end);
            const size_t n = Utf8Bytes(cp);
            if (bytes + n >= capacity) break;
            bytes += n; start = prev;
        }
        size_t written = 0;
        for (size_t i = start; i < visible_;) written += Encode(Codepoint(i), out + written);
        out[written] = 0;
        // Reclaim already displayed prefixes on the next cumulative update.
        // The retained tail gives the two-line renderer enough context to wrap.
        if (visible_ > 128) {
            size_t drop = visible_ - 128;
            if (Low(text_[drop])) --drop;
            std::memmove(text_, text_ + drop, (length_ - drop) * sizeof text_[0]);
            base_ += drop; length_ -= drop; visible_ -= drop;
        }
        return true;
    }
    bool Overflow() const { return overflow_; }
    uint32_t VisibleUnits() const { return shown_units_; }
    uint32_t TotalUnits() const { return total_units_; }
private:
    void ClearText() { length_ = visible_ = base_ = 0; shown_units_ = total_units_ = 0; overflow_ = false; }
    void CopyId(const char* id) {
        const size_t n = id ? std::min(std::strlen(id), sizeof id_ - 1) : 0;
        if (n) std::memcpy(id_, id, n);
        id_[n] = 0;
    }
    static bool High(uint16_t x) { return x >= 0xd800 && x <= 0xdbff; }
    static bool Low(uint16_t x) { return x >= 0xdc00 && x <= 0xdfff; }
    uint32_t Codepoint(size_t& i) const {
        const uint16_t first = text_[i++];
        if (High(first) && i < length_ && Low(text_[i]))
            return 0x10000 + ((first - 0xd800) << 10) + (text_[i++] - 0xdc00);
        return first;
    }
    static uint32_t Decode(const uint8_t*& p) {
        const uint8_t first = *p++;
        if (first < 0x80) return first;
        unsigned left = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : first >= 0xc2 ? 1 : 0;
        if (!left || first > 0xf4) return 0xfffd;
        uint32_t cp = first & ((1u << (6 - left)) - 1);
        while (left--) {
            if ((*p & 0xc0) != 0x80) return 0xfffd;
            cp = (cp << 6) | (*p++ & 63);
        }
        return cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff) ? cp : 0xfffd;
    }
    static uint32_t Weight(uint32_t cp) {
        if (cp == ' ' || cp == '\n' || cp == '\t') return 0;
        if (cp < 0x80) return (cp == ',' || cp == '.' || cp == '!' || cp == '?') ? 500 : 200;
        if (cp == 0x3002 || cp == 0xff0c || cp == 0xff01 || cp == 0xff1f || cp == 0x3001) return 500;
        return 1000;
    }
    static size_t Utf8Bytes(uint32_t cp) { return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; }
    static size_t Encode(uint32_t cp, char* out) {
        const size_t n = Utf8Bytes(cp);
        if (n == 1) { out[0] = static_cast<char>(cp); return 1; }
        for (size_t i = n - 1; i; --i) { out[i] = static_cast<char>(0x80 | (cp & 63)); cp >>= 6; }
        out[0] = static_cast<char>((n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | cp);
        return n;
    }
    uint16_t text_[kCapacity]{};
    char id_[80]{};
    size_t length_ = 0, visible_ = 0, base_ = 0;
    uint64_t budget_ = 0;
    uint32_t shown_units_ = 0, total_units_ = 0, rate_ = 4500;
    uint32_t last_anchor_samples_ = 0, last_anchor_units_ = 0, last_played_ = 0, end_samples_ = 0;
    uint32_t released_ = 0, heard_ = 0;
    int64_t released_at_ = 0;
    bool active_ = false, suppressed_ = false, ended_ = false, dirty_ = false, overflow_ = false;
};
static_assert(sizeof(Captions) <= 2304, "Keep captions within the C3 memory budget");
} // namespace agentlink::realtime
