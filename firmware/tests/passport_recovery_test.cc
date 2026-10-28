#include "realtime_recovery.h"
#include "passport_language.h"
using passport_wifi::TextId;
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

constexpr const char* TAG = "test";
template <typename... Args> void Log(Args...) {}
#define ESP_LOGI(...) Log(__VA_ARGS__)
#define ESP_LOGW(...) Log(__VA_ARGS__)
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = 1, ESP_ERR_INVALID_STATE = 2;
constexpr int AGENT_STREAM_VOICE = 0, AGENT_TRANSPORT_WIFI = 1;
enum State { AGENT_STATE_DISCONNECTED, AGENT_STATE_CONNECTED, AGENT_STATE_READY };
using agent_stream_handle_t = void*;
State link_state = AGENT_STATE_READY;
std::function<void()> state_hook, enabled_hook, delay_hook, read_hook, open_hook;
State agent_link_state() {
    const auto state = link_state;
    if (state_hook) { auto hook = std::move(state_hook); state_hook = {}; hook(); }
    return state;
}
std::atomic<bool> s_run{true}, s_ready{true};
std::atomic<uint32_t> s_epoch{1};
std::atomic<void*> s_task{nullptr};
agentlink::realtime::RecoveryEpoch s_recovery;
// INSERT_RECOVERING
bool agent_transport_wifi_realtime_recovering() { return cloud_realtime_recovering(); }
bool s_tx = true;
struct { int transport = AGENT_TRANSPORT_WIFI; } s_cfg;
// INSERT_PUBLIC_QUERY
int64_t now_us = 120000000;
int64_t esp_timer_get_time() { return now_us; }
void vTaskDelay(unsigned ms) { now_us += ms * 1000LL; if (delay_hook) { auto hook = delay_hook; hook(); } }
void vTaskDelete(void*) {}
unsigned cancels = 0, opens = 0, closes = 0, writes = 0;
int open_result = ESP_OK, write_result = ESP_OK;
int agent_link_end_realtime() { ++cancels; ++s_epoch; s_ready = false; return ESP_OK; }
int agent_link_stream_open(int, void*, agent_stream_handle_t* out) {
    ++opens;
    if (open_hook) open_hook();
    if (open_result == ESP_OK) *out = reinterpret_cast<void*>(size_t(opens));
    return open_result;
}
int agent_link_stream_close(agent_stream_handle_t, bool complete) {
    assert(complete); ++closes; return ESP_OK;
}
int agent_link_stream_write(agent_stream_handle_t, const void*, size_t) { ++writes; return write_result; }
struct Codec {
    bool on = false, fail_start = false;
    bool MicOn() const { return on; }
    int StartMic() { if (fail_start) return 3; on = true; return ESP_OK; }
    int StopMic() { on = false; return ESP_OK; }
    int ReadPcm(int16_t*, size_t, size_t* got) {
        *got = 320;
        if (read_hook) read_hook();
        return ESP_OK;
    }
};
struct { Codec codec; } hardware;
class PassportWifiAudio {
public:
    decltype(hardware)* hardware_ = &hardware;
    std::atomic<bool> enabled_{true}, microphone_failed_{false};
    struct { bool Muted(int64_t) { return false; } } echo_;
    bool Enabled() const { if (enabled_hook) enabled_hook(); return enabled_.load(); }
    bool MicrophoneFailed() const { return microphone_failed_; }
    void Enable(bool value) { if (value) microphone_failed_ = false; enabled_ = value; }
    void Stop() { enabled_ = false; }
    void MicLoop();
};
// INSERT_MIC_LOOP
struct App {
    PassportWifiAudio audio_;
    bool calling_ = true, active_ = true, recovering_ = false;
    int64_t requested_at_ = 0;
    char message_[144]{};
    void Message(TextId id) { snprintf(message_, sizeof message_, "%s", passport_wifi::Tr(id)); }
    // INSERT_END_CALL
    // INSERT_UPDATE_CALL
};
struct Grant {
    bool direct = false, platform = true;
    uint32_t epoch = 1;
    int agent_id = 1, audio = 0;
    char session_id[8] = "fixture", url[8]{}, token[8]{};
};
int s_grants = 1;
bool dequeued = false;
bool Current(uint32_t epoch) { return s_run && s_epoch == epoch; }
int xQueueReceive(int, Grant* grant, unsigned) {
    assert(!dequeued); dequeued = true; *grant = Grant{}; return pdTRUE;
}
void ResetInput() {}
struct Callbacks {
    void (*state)(bool, const char*) = nullptr;
    void (*audio_reset)() = nullptr;
    void (*finished)() = nullptr;
} s_cb;
unsigned grants = 0, connects = 0, cleanups = 0;
bool grant_ok = true;
std::function<bool()> connect;
App* observed = nullptr;
int cloud_platform_grant(int, const char*, bool, char* url, size_t, char* token, size_t, int*, int* status) {
    ++grants; *status = grant_ok ? 200 : 500;
    // Fresh one-use credentials on every retry; previous credentials were cleared.
    assert(!*url && !*token); strcpy(url, "url"); strcpy(token, "token");
    if (observed) observed->UpdateCall(now_us);
    return grant_ok ? ESP_OK : 3;
}
void cloud_platform_end_grant(int, const char*) {
    ++cleanups; if (observed) observed->UpdateCall(now_us);
}
bool ConnectOnce(Grant&) { ++connects; return connect(); }
// INSERT_WORKER
struct Done {};
void Reset() {
    state_hook = enabled_hook = delay_hook = read_hook = open_hook = {};
    s_run = s_ready = true; s_epoch = 1; s_recovery.End(1); s_recovery.End(2);
    s_tx = true; s_cfg.transport = AGENT_TRANSPORT_WIFI; link_state = AGENT_STATE_READY;
    cancels = opens = closes = writes = grants = connects = cleanups = 0;
    open_result = write_result = ESP_OK; grant_ok = true; dequeued = false; observed = nullptr;
    now_us = 120000000; hardware.codec = {};
}
void RunWorker(App& app) {
    observed = &app;
    s_cb.audio_reset = [] {
        // The recovery intent must already be visible before audio callbacks.
        observed->UpdateCall(now_us);
    };
    s_cb.state = [](bool ready, const char* hint) {
        assert(!ready);
        link_state = AGENT_STATE_CONNECTED;
        if (strstr(hint, "Reconnecting")) assert(agent_link_realtime_recovering());
        observed->UpdateCall(now_us);
    };
    s_cb.finished = [] { s_run = false; };
    Worker(nullptr);
}
int main() {
    Reset();
    s_recovery.Begin(1);
    assert(agent_link_realtime_recovering());
    s_tx = false; assert(!agent_link_realtime_recovering()); s_tx = true;
    s_cfg.transport = 0; assert(!agent_link_realtime_recovering()); s_cfg.transport = AGENT_TRANSPORT_WIFI;
    ++s_epoch; assert(!agent_link_realtime_recovering());
    s_recovery.Begin(2); s_recovery.End(1); assert(agent_link_realtime_recovering());
    s_recovery.End(2); assert(!agent_link_realtime_recovering());

    // Actual worker + UI: an established call (>30 s) survives both backoffs,
    // resumes capture, then honors an explicit terminal end on attempt three.
    Reset(); App app;
    unsigned backoff_ticks = 0;
    delay_hook = [&] {
        app.UpdateCall(now_us); ++backoff_ticks;
        assert(app.calling_ && app.recovering_ && !app.audio_.Enabled() && !cancels);
    };
    connect = [&] {
        if (connects > 1) {
            assert(app.calling_ && agent_link_realtime_recovering());
            link_state = AGENT_STATE_READY;
            app.UpdateCall(now_us); assert(!app.audio_.Enabled());
            // session.ready clears recovery only after the transport callback.
            // The actual Call implementation is exercised in --voice-memory.
            s_recovery.End(1); app.UpdateCall(now_us);
            assert(app.active_ && app.audio_.Enabled() && !app.recovering_ && !cancels);
        }
        return connects < 3;
    };
    RunWorker(app);
    assert(connects == 3 && grants == 3 && cleanups == 3 && backoff_ticks == 120);
    assert(!app.calling_ && cancels == 1 && !agent_link_realtime_recovering());

    // Exhaustion (three transport failures) and rejected fresh grant both stop.
    for (bool reject_grant : {false, true}) {
        Reset(); App exhausted;
        connect = [&] { if (reject_grant) grant_ok = false; return true; };
        delay_hook = [&] { exhausted.UpdateCall(now_us); assert(exhausted.calling_ && !cancels); };
        RunWorker(exhausted);
        assert(grants == (reject_grant ? 2U : 3U));
        assert(!exhausted.calling_ && cancels == 1 && !agent_link_realtime_recovering());
    }
    Reset(); App canceled;
    connect = [] { return true; };
    delay_hook = [&] { canceled.EndCall(); delay_hook = {}; };
    RunWorker(canceled);
    assert(grants == 1 && connects == 1 && cancels == 1 && !agent_link_realtime_recovering());

    // Ready/recovery transition in the middle of a UI tick must not cancel.
    Reset(); App raced;
    s_recovery.Begin(1); link_state = AGENT_STATE_CONNECTED;
    state_hook = [] { link_state = AGENT_STATE_READY; s_recovery.End(1); };
    raced.UpdateCall(now_us); assert(raced.calling_ && !cancels);
    enabled_hook = [&] { s_recovery.Begin(1); link_state = AGENT_STATE_CONNECTED; raced.audio_.Enable(false); };
    raced.UpdateCall(now_us); assert(raced.calling_ && !cancels);
    enabled_hook = {}; raced.UpdateCall(now_us); assert(raced.recovering_);
    Reset(); App failed_mic;
    failed_mic.audio_.enabled_ = false; failed_mic.audio_.microphone_failed_ = true;
    failed_mic.UpdateCall(now_us); assert(!failed_mic.calling_ && cancels == 1);
    Reset(); App starting; starting.active_ = false; link_state = AGENT_STATE_CONNECTED;
    starting.requested_at_ = now_us;
    starting.UpdateCall(now_us + 29000000); assert(starting.calling_);
    starting.UpdateCall(now_us + 31000000); assert(!starting.calling_ && cancels == 1);

    // Actual microphone loop: invalid state at open or after ReadPcm is transient.
    for (bool at_open : {false, true}) {
        Reset(); PassportWifiAudio mic;
        if (at_open) {
            open_result = ESP_ERR_INVALID_STATE;
            delay_hook = [&] {
                assert(mic.Enabled() && !cancels); open_result = ESP_OK; delay_hook = {};
            };
            read_hook = [&] { assert(opens == 2 && mic.Enabled()); throw Done{}; };
        } else {
            read_hook = [&] {
                if (opens == 1) { link_state = AGENT_STATE_CONNECTED; write_result = ESP_ERR_INVALID_STATE; }
                else { assert(closes == 1 && mic.Enabled()); throw Done{}; }
            };
            delay_hook = [&] {
                assert(mic.Enabled() && !hardware.codec.on && !cancels);
                link_state = AGENT_STATE_READY; write_result = ESP_OK;
            };
        }
        try { mic.MicLoop(); assert(false); } catch (Done&) {}
        assert(opens == 2 && !cancels);
    }
    Reset(); PassportWifiAudio stopped;
    read_hook = [&] { stopped.Stop(); };
    delay_hook = [&] { assert(!stopped.Enabled() && !hardware.codec.on && closes == 1); throw Done{}; };
    try { stopped.MicLoop(); assert(false); } catch (Done&) {}
    Reset(); PassportWifiAudio broken;
    hardware.codec.fail_start = true;
    delay_hook = [&] { assert(broken.MicrophoneFailed() && !broken.Enabled()); throw Done{}; };
    try { broken.MicLoop(); assert(false); } catch (Done&) {}
    std::puts("Passport worker/UI/microphone recovery regression scenarios passed");
}
