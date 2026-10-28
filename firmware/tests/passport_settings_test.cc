#include "passport_settings.h"
#include "passport_wifi_controls.h"
#include "nvs.h"
#include "esp_log.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
using namespace passport_wifi;
constexpr const char* TAG = "test";
using Values = std::map<std::string, std::string>;
std::map<std::string, Values> store;
Values pending;
std::string current_ns, failure;
bool stopped = false;
unsigned stops = 0, starts = 0, erases = 0, reboots = 0, commits = 0;
int nvs_open(const char* ns, int, nvs_handle_t* handle) {
    if (failure == "open") return ESP_FAIL;
    current_ns = ns; pending = store[ns]; *handle = 1; return ESP_OK;
}
int nvs_set_u8(nvs_handle_t, const char* key, uint8_t value) {
    if (failure == "set") return ESP_FAIL;
    pending[key] = std::to_string(value); return ESP_OK;
}
int nvs_commit(nvs_handle_t) {
    if (failure == "commit") return ESP_FAIL;
    store[current_ns] = pending; ++commits; return ESP_OK;
}
int nvs_erase_all(nvs_handle_t) {
    assert(stopped && current_ns == "al_wifi"); ++erases;
    if (failure == "erase") return ESP_FAIL;
    pending.clear(); return ESP_OK;
}
void nvs_close(nvs_handle_t) { pending.clear(); }
int nvs_get_u8(nvs_handle_t, const char* key, uint8_t* value) {
    if (!pending.count(key)) return ESP_ERR_NVS_NOT_FOUND;
    *value = std::stoi(pending[key]); return ESP_OK;
}
int nvs_get_i32(nvs_handle_t, const char* key, int32_t* value) {
    if (!pending.count(key)) return ESP_ERR_NVS_NOT_FOUND;
    *value = std::stoi(pending[key]); return ESP_OK;
}
int nvs_get_str(nvs_handle_t, const char* key, char* out, size_t* size) {
    if (!pending.count(key)) return ESP_ERR_NVS_NOT_FOUND;
    const auto& value = pending[key]; assert(value.size() < *size);
    strcpy(out, value.c_str()); *size = value.size() + 1; return ESP_OK;
}
void agent_link_stop() { stopped = true; ++stops; }
int agent_link_start() { stopped = false; ++starts; return ESP_OK; }
void esp_restart() { assert(stopped && store["al_wifi"].empty()); ++reboots; }
namespace raster {
// CopyText is exercised separately by menu/render tests; this fixture supplies
// it so no font assets are needed for persistence and action tests.
void CopyText(char* out, size_t capacity, const char* text) { snprintf(out, capacity, "%s", text); }
}
namespace passport_wifi { void SetAnswer(const char*) {} }
enum class Page { Home, Menu, Sessions, ConfirmWifi, Language };
enum class Action { Sessions, New, Back, Select, Previous, Next, Wifi, ClearWifi, Language, Chinese, English };
struct Item { Action action; unsigned row; char name[64]; };
struct App {
    Page page_ = Page::Home;
    Item items_[6]{};
    size_t count_ = 0, cursor_ = 0, offset_ = 0, fetched_ = 0;
    std::atomic<bool> menu_{false};
    std::atomic<int> volume_{70};
    std::atomic<uint32_t> view_{1};
    char message_[144]{}, session_[80]{}, session_name_[96]{};
    int32_t saved_agent_ = 0;
    bool calling_ = false;
    void EndCall() { calling_ = false; }
    void Error(int) { snprintf(message_, sizeof message_, "%s", Tr(TextId::RequestFailed)); }
    void StartCall() { assert(false); }
    bool SyncAgent(bool) { return false; }
    void Busy(const char*) {}
    bool CreateSession() { assert(false); return false; }
    void Sessions(size_t = 0) { assert(false); }
    void SelectSession(unsigned) { assert(false); }
    // INSERT_LOAD
    // INSERT_PAGES
    // INSERT_HANDLE
    void Choose(Action action) {
        for (size_t i = 0; i < count_; ++i) if (items_[i].action == action) {
            cursor_ = i; Handle(Gesture::Click); return;
        }
        assert(false);
    }
};
extern "C" bool al_wifi_prov_use_english();
// INSERT_REPLACE
std::string html, rendered;
const char* portal_html_start = nullptr;
std::mutex s_scan_mtx;
std::string s_scan_options;
struct httpd_req_t {};
int httpd_resp_set_type(httpd_req_t*, const char* type) { assert(strcmp(type, "text/html; charset=utf-8") == 0); return ESP_OK; }
int httpd_resp_send(httpd_req_t*, const char* text, size_t length) { rendered.assign(text, length); return ESP_OK; }
// INSERT_PORTAL
void Reset() {
    store = {{"al_wifi", {{"ssid", "synthetic-network"}, {"pass", "synthetic-password"}}},
             {"passport_wifi", {{"agent", "7"}, {"session", "synthetic-session"}, {"name", "天依"}, {"volume", "65"}, {"language", "0"}}},
             {"al_dev", {{"key", "synthetic-device-key"}}}, {"agent_link", {{"uuid", "synthetic-identity"}}}};
    failure.clear(); stopped = false; stops = starts = erases = reboots = commits = 0;
    SetLanguage(Language::Chinese);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    Reset(); App app;
    app.Handle(Gesture::Hold); assert(app.page_ == Page::Menu && !erases && !reboots);
    app.Choose(Action::Wifi); assert(app.page_ == Page::ConfirmWifi && !erases && !stops);
    app.Handle(Gesture::Click); // Confirmation defaults to cancel.
    assert(app.page_ == Page::Menu && !erases && !reboots);
    app.Choose(Action::Wifi); app.Handle(Gesture::Hold);
    assert(app.page_ == Page::Home && !erases);
    app.Menu();
    for (unsigned i = 0; i < 3; ++i) app.Handle(Gesture::Down);
    app.Handle(Gesture::Click); assert(app.page_ == Page::Language);
    app.Handle(Gesture::Down); app.Handle(Gesture::Click);
    assert(GetLanguage() == Language::English && al_wifi_prov_use_english());
    assert(store["passport_wifi"]["language"] == "1");
    assert(strcmp(app.items_[0].name, "Chinese") == 0 && strcmp(app.items_[1].name, "English") == 0);
    SetLanguage(Language::Chinese); App rebooted; rebooted.Load();
    assert(GetLanguage() == Language::English && rebooted.volume_ == 65 && rebooted.saved_agent_ == 7);
    store["passport_wifi"]["language"] = "255"; rebooted.Load(); assert(GetLanguage() == Language::Chinese);
    store["passport_wifi"].erase("language"); SetLanguage(Language::English);
    rebooted.Load(); assert(GetLanguage() == Language::Chinese);
    for (const char* fail : {"open", "set", "commit"}) {
        Reset(); failure = fail; App failed; failed.Menu(); failed.Choose(Action::Language); failed.Choose(Action::English);
        assert(GetLanguage() == Language::Chinese && store["passport_wifi"]["language"] == "0" && failed.message_[0]);
    }
    for (const char* fail : {"open", "erase", "commit", ""}) {
        Reset(); const auto before = store; failure = fail;
        App wifi; wifi.Menu(); wifi.Choose(Action::Wifi); wifi.Choose(Action::ClearWifi);
        assert(stops == 1);
        for (const char* ns : {"passport_wifi", "al_dev", "agent_link"}) assert(store[ns] == before.at(ns));
        if (*fail) assert(!reboots && starts == 1 && !stopped && wifi.message_[0] && store["al_wifi"] == before.at("al_wifi"));
        else assert(reboots == 1 && !starts && store["al_wifi"].empty());
    }
    for (const auto language : {Language::Chinese, Language::English}) {
        SetLanguage(language);
        for (const char* file : {"portal.html", "connecting.html"}) {
            std::ifstream input(std::string(argv[1]) + '/' + file);
            html.assign(std::istreambuf_iterator<char>(input), {}); assert(!html.empty());
            portal_html_start = html.c_str();
            s_scan_options = "<option value=\"{{TITLE}}\">{{TITLE}}</option>";
            httpd_req_t request; assert(RootGet(&request) == ESP_OK);
            assert(rendered.find(language == Language::English ? "lang=\"en\"" : "lang=\"zh-CN\"") != std::string::npos);
            assert((rendered.find("id=\"lang\" checked") != std::string::npos) == (language == Language::English));
            assert(rendered.find("{{LANG_CHECKED}}") == std::string::npos && rendered.find("{{LOCALE}}") == std::string::npos);
            if (strcmp(file, "portal.html") == 0) assert(rendered.find(s_scan_options) != std::string::npos);
        }
    }
    puts("Passport actual menu confirmation, persisted language, Wi-Fi-only erase/failures and portal locale: PASS");
}
