#include "application.h"
#include "ai_passport_hardware.h"
#include "agent_link_platform.h"
#include "platform_config.h"
#include "platform_product.h"
#include "passport_wifi_audio.h"
#include "passport_wifi_controls.h"
#include "passport_wifi_ui.h"
#include "passport_settings.h"
#include "esp_system.h"
#include "stripe_canvas.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

extern "C" const raster_font_t passport_font_16;

namespace {
constexpr const char* TAG = "Passport.wifi";
using passport_wifi::Gesture;
using passport_wifi::TextId;
using passport_wifi::Tr;
using passport_wifi::Language;
using passport_wifi::VoiceState;
enum class Page { Home, Menu, Sessions, ConfirmWifi, Language };
enum class Action { Sessions, New, Back, Select, Previous, Next, Wifi, ClearWifi, Language, Chinese, English };
struct Item { Action action; unsigned row; char name[64]; };
struct Input { Gesture gesture; uint32_t view; };
}

class AiPassportWifi : public Application {
public:
    AiPassportWifi() {
        ESP_ERROR_CHECK(nvs_flash_init());
        Load();
        hardware_.Init();
        if (hardware_.audio_ready) (void)hardware_.codec.SetVolume(volume_.load());
        if (audio_.Init(&hardware_) != ESP_OK) ESP_LOGE(TAG, "Audio unavailable");
        if (hardware_.display_ready && passport_wifi::StartUi(&hardware_.lcd) != ESP_OK)
            ESP_LOGE(TAG, "UI unavailable");
        events_ = xQueueCreate(8, sizeof(Input));
        ESP_ERROR_CHECK(events_ ? ESP_OK : ESP_ERR_NO_MEM);
        ESP_ERROR_CHECK(xTaskCreate(ControlEntry, "passport_ctrl", 6144, this, 4, nullptr) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);
        if (hardware_.keys_ready)
            ESP_ERROR_CHECK(xTaskCreate(KeyEntry, "passport_keys", 3072, this, 4, nullptr) == pdPASS
                            ? ESP_OK : ESP_ERR_NO_MEM);
        ESP_LOGI(TAG, "WiFi voice ready: product=%d, internal free=%u largest=%u",
                 AGENT_LINK_PRODUCT_ID,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    }
    const char* Name() const override { return "AIPassport"; }
    const char* Model() const override { return "AI-PASSPORT"; }
    const agent_platform_t* Platform() const override {
        static const agent_platform_t platform = {
            .base_url = DEOTALAND_PLATFORM_BASE_URL, .product_id = AGENT_LINK_PRODUCT_ID,
            .chip_type = "esp32-c3",
        };
        return &platform;
    }
    uint32_t Capabilities() const override {
        uint32_t caps = hardware_.keys_ready ? AGENT_CAP_BUTTON : 0;
        if (hardware_.display_ready) caps |= AGENT_CAP_SCREEN;
        if (audio_.Ready()) caps |= AGENT_CAP_MIC | AGENT_CAP_SPEAKER;
        return caps;
    }
    void OnLinkStatus(const agent_link_status_t& status) override { passport_wifi::SetStatus(status); }
    void PlayAudio(const uint8_t* pcm, size_t bytes) override { audio_.Play(pcm, bytes); }
    void AudioEnd() override { audio_.End(); }
    void AudioReset() override { audio_.Reset(); }
    void ShowText(const char* text) override { passport_wifi::SetAnswer(text); }
    void OnListen(bool start, uint32_t) override {
        if (!start) audio_.Enable(false);
    }
private:
    static void ControlEntry(void* ctx) { static_cast<AiPassportWifi*>(ctx)->ControlLoop(); }
    static void KeyEntry(void* ctx) { static_cast<AiPassportWifi*>(ctx)->KeyLoop(); }
    void Load() {
        nvs_handle_t handle;
        if (nvs_open("passport_wifi", NVS_READONLY, &handle) != ESP_OK) return;
        (void)nvs_get_i32(handle, "agent", &saved_agent_);
        size_t size = sizeof session_; (void)nvs_get_str(handle, "session", session_, &size);
        size = sizeof session_name_; (void)nvs_get_str(handle, "name", session_name_, &size);
        uint8_t volume = 70; (void)nvs_get_u8(handle, "volume", &volume);
        volume_.store(volume > 100 ? 100 : volume);
        uint8_t language = 0; (void)nvs_get_u8(handle, "language", &language);
        passport_wifi::SetLanguage(passport_wifi::StoredLanguage(language));
        nvs_close(handle);
        if (saved_agent_ <= 0) session_[0] = session_name_[0] = 0;
    }
    void Save() {
        nvs_handle_t handle;
        if (nvs_open("passport_wifi", NVS_READWRITE, &handle) != ESP_OK) return;
        esp_err_t result = nvs_set_i32(handle, "agent", saved_agent_);
        if (result == ESP_OK) result = nvs_set_str(handle, "session", session_);
        if (result == ESP_OK) result = nvs_set_str(handle, "name", session_name_);
        if (result == ESP_OK) result = nvs_set_u8(handle, "volume", volume_.load());
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
        if (result != ESP_OK) ESP_LOGW(TAG, "Settings save failed: %s", esp_err_to_name(result));
    }
    void EndCall() {
        calling_ = active_ = recovering_ = false;
        requested_at_ = 0;
        audio_.Stop();
        (void)agent_link_end_realtime();
    }
    void Message(TextId id) { raster::CopyText(message_, sizeof message_, Tr(id)); }
    void Error(esp_err_t result) {
        Message(TextId::RequestFailed);
        ESP_LOGW(TAG, "Platform request: %s", esp_err_to_name(result));
    }
    bool SyncAgent(bool explain = false) {
        agent_link_platform_agent_t bound{};
        const esp_err_t result = agent_link_platform_bound_agent(&bound);
        if (result != ESP_OK) {
            if (agent_) { EndCall(); agent_ = 0; }
            if (explain) Message(TextId::ActivateFirst);
            return false;
        }
        if (agent_ != bound.id) {
            EndCall(); agent_ = bound.id;
            if (saved_agent_ != bound.id) {
                session_[0] = session_name_[0] = 0;
                saved_agent_ = bound.id; Save();
            }
            if (page_ == Page::Sessions) SetPage(Page::Home);
        }
        voice_supported_ = bound.voice_supported;
        raster::CopyText(agent_name_, sizeof agent_name_, bound.name);
        if (explain && !voice_supported_)
            Message(TextId::NoVoice);
        return voice_supported_;
    }
    void SetPage(Page page) {
        page_ = page; count_ = cursor_ = 0; message_[0] = 0;
        menu_.store(page != Page::Home); view_.fetch_add(1);
    }
    void Add(Action action, const char* name, unsigned row = 0) {
        if (count_ >= 6) return;
        Item& item = items_[count_++]; item.action = action; item.row = row;
        raster::CopyText(item.name, sizeof item.name, name);
        for (char* p = item.name; *p; ++p)
            if (*p == '\r' || *p == '\n' || *p == '\t') *p = ' ';
    }
    void Menu() {
        EndCall(); SetPage(Page::Menu); passport_wifi::SetAnswer("");
        Add(Action::Sessions, Tr(TextId::ChooseConversation));
        Add(Action::New, Tr(TextId::NewConversation));
        Add(Action::Wifi, Tr(TextId::ChangeWifi));
        Add(Action::Language, Tr(TextId::LanguageTitle));
        Add(Action::Back, Tr(TextId::Back));
    }
    void LanguageMenu() {
        SetPage(Page::Language);
        Add(Action::Chinese, Tr(TextId::ChineseOption));
        Add(Action::English, Tr(TextId::EnglishOption));
        Add(Action::Back, Tr(TextId::Back));
    }
    bool Fetch(size_t offset) {
        size_t count = 0, total = 0;
        const esp_err_t result = agent_link_platform_conversations(agent_, offset, sessions_, 3, &count, &total);
        if (result != ESP_OK) { Error(result); return false; }
        fetched_ = count; offset_ = offset; total_ = total;
        return true;
    }
    void Sessions(size_t offset = 0) {
        if (!SyncAgent(true)) return;
        EndCall(); Busy(Tr(TextId::Loading));
        if (!Fetch(offset)) return;
        if (!fetched_ && offset && !Fetch(0)) return;
        SetPage(Page::Sessions);
        for (size_t i = 0; i < fetched_; ++i)
            Add(Action::Select, sessions_[i].name[0] ? sessions_[i].name : Tr(TextId::Conversation), i);
        if (offset_) Add(Action::Previous, Tr(TextId::Previous));
        if (total_ > offset_ + fetched_) Add(Action::Next, Tr(TextId::Next));
        Add(Action::Back, Tr(TextId::Back));
        if (!fetched_) Message(TextId::NoConversations);
    }
    void Busy(const char* title) {
        view_.fetch_add(1);
        passport_wifi::SetContent(title, Tr(TextId::PleaseWait), Tr(TextId::VolumeKeys), passport_wifi::View::Menu);
    }
    void SelectSession(size_t row) {
        snprintf(session_, sizeof session_, "%s", sessions_[row].id);
        raster::CopyText(session_name_, sizeof session_name_, sessions_[row].name);
        saved_agent_ = agent_; Save(); SetPage(Page::Home);
    }
    bool CreateSession() {
        char id[sizeof session_]{};
        const esp_err_t result = agent_link_platform_create_conversation(agent_, id, sizeof id);
        if (result != ESP_OK) { Error(result); return false; }
        snprintf(session_, sizeof session_, "%s", id);
        raster::CopyText(session_name_, sizeof session_name_, Tr(TextId::NewConversation));
        saved_agent_ = agent_; Save(); return true;
    }
    void StartCall() {
        ESP_LOGI(TAG, "call setup begin");
        if (!audio_.Ready()) { Message(TextId::AudioUnavailable); return; }
        if (!SyncAgent(true)) return;
        if (agent_link_state() != AGENT_STATE_CONNECTED) {
            Message(TextId::WaitingConnection); return;
        }
        Busy(Tr(TextId::PreparingVoice));
        if (!session_[0]) {
            if (!Fetch(0)) return;
            if (fetched_) SelectSession(0);
            else if (!CreateSession()) return;
        }
        // APIs run on this worker. Recheck binding after HTTPS, before requesting a grant.
        agent_link_platform_agent_t bound{};
        if (agent_link_platform_bound_agent(&bound) != ESP_OK || bound.id != agent_) {
            Message(TextId::BindingChanged); return;
        }
        const esp_err_t result = agent_link_request_realtime_session(agent_, session_);
        if (result != ESP_OK) { Error(result); return; }
        passport_wifi::SetAnswer(""); message_[0] = 0;
        calling_ = true; active_ = recovering_ = false; requested_at_ = esp_timer_get_time();
    }
    void Handle(Gesture gesture) {
        if (gesture == Gesture::Click || gesture == Gesture::Hold)
            ESP_LOGI(TAG, "OK gesture handled: %u", unsigned(gesture));
        message_[0] = 0;
        if (gesture == Gesture::Hold) { if (page_ == Page::Home) Menu(); else SetPage(Page::Home); return; }
        if (page_ == Page::Home) {
            if (gesture == Gesture::Click) { if (calling_) EndCall(); else StartCall(); }
            return;
        }
        if (gesture == Gesture::Up && cursor_) --cursor_;
        else if (gesture == Gesture::Down && cursor_ + 1 < count_) ++cursor_;
        else if (gesture == Gesture::Click && count_) {
            const Item item = items_[cursor_];
            switch (item.action) {
            case Action::Sessions: Sessions(); break;
            case Action::New:
                if (SyncAgent(true)) { Busy(Tr(TextId::Creating)); if (CreateSession()) SetPage(Page::Home); }
                break;
            case Action::Wifi:
                SetPage(Page::ConfirmWifi);
                Add(Action::Back, Tr(TextId::Cancel));
                Add(Action::ClearWifi, Tr(TextId::ClearWifi));
                break;
            case Action::ClearWifi: {
                if (page_ != Page::ConfirmWifi) break;
                Busy(Tr(TextId::ResettingWifi));
                const esp_err_t result = passport_wifi::ClearWifi();
                if (result == ESP_OK) esp_restart();
                else Error(result);
                break;
            }
            case Action::Language: LanguageMenu(); break;
            case Action::Chinese:
            case Action::English: {
                const auto language = item.action == Action::English ? Language::English : Language::Chinese;
                const esp_err_t result = passport_wifi::SaveLanguage(language);
                if (result == ESP_OK) LanguageMenu();
                else Error(result);
                break;
            }
            case Action::Back: if (page_ != Page::Menu) Menu(); else SetPage(Page::Home); break;
            case Action::Select: SelectSession(item.row); break;
            case Action::Previous: Sessions(offset_ >= 3 ? offset_ - 3 : 0); break;
            case Action::Next: Sessions(offset_ + fetched_); break;
            }
        }
        view_.fetch_add(1);
    }
    void Publish() {
        char detail[384]{};
        if (page_ != Page::Home) {
            if (message_[0]) snprintf(detail, sizeof detail, "%s", message_);
            else for (size_t i = (cursor_ / 3) * 3; i < count_ && i < (cursor_ / 3) * 3 + 3; ++i) {
                const size_t used = strlen(detail);
                raster::TextLayout line;
                line.Wrap(passport_font_16, items_[i].name, 168);
                const int bytes = line.Count() ? line.End(0) : 0;
                snprintf(detail + used, sizeof detail - used, "%s %.*s\n\n",
                         i == cursor_ ? ">" : " ", bytes, items_[i].name);
            }
            const TextId title = page_ == Page::Menu ? TextId::SettingsTitle :
                                 page_ == Page::Language ? TextId::LanguageTitle :
                                 page_ == Page::ConfirmWifi ? TextId::ResetTitle : TextId::ConversationsTitle;
            passport_wifi::SetContent(Tr(title), detail, Tr(TextId::MenuFooter), passport_wifi::View::Menu);
        } else {
            if (message_[0]) snprintf(detail, sizeof detail, "%s", message_);
            else {
                char volume[40]; snprintf(volume, sizeof volume, Tr(TextId::Volume), volume_.load());
                snprintf(detail, sizeof detail, "%s\n\n%s\n\n%s",
                         agent_name_[0] ? agent_name_ : Tr(TextId::BindAgent),
                         session_[0] ? Tr(TextId::Selected) : Tr(TextId::ContinueLatest), volume);
            }
            const auto voice = active_ ? (audio_.Speaking() ? VoiceState::Speaking : VoiceState::Listening) :
                               calling_ ? (recovering_ ? VoiceState::Reconnecting : VoiceState::Starting) : VoiceState::Idle;
            const auto title = active_ ? (voice == VoiceState::Speaking ? TextId::Speaking : TextId::Listening) :
                               calling_ ? (recovering_ ? TextId::Retrying : TextId::StartingCall) : TextId::HomeTitle;
            passport_wifi::SetContent(Tr(title), detail, Tr(calling_ ? TextId::CallFooter : TextId::HomeFooter),
                                      message_[0] ? passport_wifi::View::Text : passport_wifi::View::Portrait,
                                      volume_.load(), voice);
        }
    }
    void UpdateCall(int64_t now) {
        if (!calling_) return;
        const auto state = agent_link_state();
        if (agent_link_realtime_recovering()) {
            if (!recovering_) ESP_LOGW(TAG, "call transport interrupted; preserving recovery");
            audio_.Enable(false); active_ = false; recovering_ = true;
            return;
        }
        // Recovery may finish between the two reads. Defer the decision until
        // the next control tick instead of ending a just-restored connection.
        if (state != agent_link_state()) return;
        if (state == AGENT_STATE_READY) {
            if (active_ && !audio_.Enabled()) {
                // A stop callback may race the first recovery check above.
                if (agent_link_realtime_recovering() || agent_link_state() != AGENT_STATE_READY) return;
                const bool failed = audio_.MicrophoneFailed();
                EndCall(); Message(failed ? TextId::MicrophoneFailed : TextId::VoiceDisconnected);
            } else if (!active_) {
                if (recovering_) ESP_LOGI(TAG, "call transport recovered; resuming microphone");
                audio_.Enable(true); active_ = true; recovering_ = false;
            }
        } else if (recovering_ || active_ || state == AGENT_STATE_DISCONNECTED || now - requested_at_ > 30000000) {
            ESP_LOGI(TAG, "call ended: state=%u recovered_wait=%u", unsigned(state), unsigned(recovering_));
            EndCall(); Message(TextId::CallEnded);
        }
    }
    void UpdateBattery(int64_t now) {
        if (battery_at_ >= 0 && now - battery_at_ < 5000000) return;
        battery_at_ = now;
        passport_wifi::SetBattery(hardware_.battery.ReadPercent());
    }
    void ControlLoop() {
        int64_t sync_at = 0;
        int saved_volume = volume_.load();
        for (;;) {
            Input input{};
            if (xQueueReceive(events_, &input, pdMS_TO_TICKS(100)) == pdTRUE && input.view == view_.load())
                Handle(input.gesture);
            const int64_t now = esp_timer_get_time();
            if (now - sync_at >= 1000000) {
                (void)SyncAgent(); sync_at = now;
                if (saved_volume != volume_.load()) { Save(); saved_volume = volume_.load(); }
            }
            UpdateCall(now);
            UpdateBattery(now);
            Publish();
        }
    }
    void KeyLoop() {
        passport_wifi::Controls controls;
        PassportKey candidate = PassportKey::None, held = PassportKey::None;
        uint32_t since = 0;
        for (;;) {
            const uint32_t now = esp_timer_get_time() / 1000;
            const PassportKey sample = hardware_.ReadKey();
            if (sample != candidate) { candidate = sample; since = now; }
            if (uint32_t(now - since) >= 25) held = candidate;
            const Gesture gesture = controls.Update(static_cast<passport_wifi::Key>(held), now);
            if (!menu_.load() && (gesture == Gesture::Up || gesture == Gesture::Down)) {
                int volume = volume_.load() + (gesture == Gesture::Up ? 5 : -5);
                volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
                if (hardware_.audio_ready) (void)hardware_.codec.SetVolume(volume);
                volume_.store(volume);
            } else if (gesture != Gesture::None) {
                if (gesture == Gesture::Click || gesture == Gesture::Hold)
                    ESP_LOGI(TAG, "OK gesture posted: %u", unsigned(gesture));
                Input input{gesture, view_.load()}; (void)xQueueSend(events_, &input, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    AiPassportHardware hardware_;
    PassportWifiAudio audio_;
    QueueHandle_t events_ = nullptr;
    std::atomic<int> volume_{70};
    std::atomic<bool> menu_{false};
    std::atomic<uint32_t> view_{1};
    Page page_ = Page::Home;
    Item items_[6]{};
    size_t count_ = 0, cursor_ = 0, fetched_ = 0, offset_ = 0, total_ = 0;
    agent_link_platform_conversation_t sessions_[3]{};
    int agent_ = 0;
    int32_t saved_agent_ = 0;
    char session_[80]{}, session_name_[96]{}, agent_name_[80]{}, message_[144]{};
    bool voice_supported_ = false, calling_ = false, active_ = false, recovering_ = false;
    int64_t requested_at_ = 0;
    int64_t battery_at_ = -1;
};

DECLARE_APPLICATION(AiPassportWifi);
