#include <atomic>
#include <cassert>
#include <string>
// INSERT_UI
enum class Page { Home, Menu, Sessions };
enum class Action { Sessions, New, Back, Select, Previous, Next };
struct Item { Action action; unsigned row; char name[64]; };
struct App {
    Page page_ = Page::Menu;
    size_t count_ = 0, cursor_ = 0;
    Item items_[6]{};
    char message_[144]{}, session_[80]{}, agent_name_[80]{};
    std::atomic<int> volume_{70};
    bool active_ = false, calling_ = false, recovering_ = false;
    struct { bool Speaking() { return false; } } audio_;
    // INSERT_ADD
    // INSERT_PUBLISH
};
struct FrameDone {};
St7789Lcd lcd;
void vTaskDelay(unsigned ticks) { if (ticks > 1 && lcd.stripes >= 80) throw FrameDone{}; }
void ValidateUtf8(const char* text) {
    const char* end = text + strlen(text);
    while (text < end) assert(raster::NextCodepoint(text, end) != 0xfffd);
}
void Render(const char* directory, const char* name) {
    lcd.stripes = 0;
    assert(passport_wifi::StartUi(&lcd) == ESP_OK);
    try { passport_wifi::Render(nullptr); assert(false); } catch (FrameDone&) {}
    const std::string path = std::string(directory) + '/' + name + ".ppm";
    FILE* file = fopen(path.c_str(), "wb"); assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    for (size_t i = 0; i < sizeof lcd.pixels; i += 2) {
        const unsigned pixel = (lcd.pixels[i] << 8) | lcd.pixels[i + 1];
        const uint8_t rgb[] = {uint8_t((pixel >> 11) * 255 / 31), uint8_t(((pixel >> 5) & 63) * 255 / 63),
                               uint8_t((pixel & 31) * 255 / 31)};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    fclose(file);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    char text[16];
    for (size_t capacity = 1; capacity <= sizeof text; ++capacity) {
        memset(text, 0x55, sizeof text);
        raster::CopyText(text, capacity, "天依 hello 世界"); ValidateUtf8(text);
        assert(strlen(text) < capacity);
        if (capacity < sizeof text) assert(text[capacity] == 0x55);
    }
    raster::CopyText(nullptr, 0, nullptr);
    raster::CopyText(text, sizeof text, nullptr); assert(!*text);
    raster::CopyText(text, sizeof text, "a\xe4\xb8"); assert(strcmp(text, "a??") == 0);
    App menu;
    menu.Add(Action::Sessions, "选择会话"); menu.Add(Action::New, "新建对话"); menu.Add(Action::Back, "返回");
    menu.Publish();
    assert(strcmp(passport_wifi::input.title, "语音菜单") == 0);
    assert(strstr(passport_wifi::input.detail, "选择会话") && strstr(passport_wifi::input.detail, "返回"));
    Render(argv[1], "menu");
    App sessions; sessions.page_ = Page::Sessions;
    sessions.Add(Action::Select, "天依陪我聊天的一个很长很长的中文会话名字");
    sessions.Add(Action::Select, "Hello 天依\n一起讲故事");
    sessions.Add(Action::Back, "返回"); sessions.cursor_ = 1; sessions.Publish();
    const char* detail = passport_wifi::input.detail;
    assert(strstr(detail, "Hello 天依 一起讲故事") && strstr(detail, "返回"));
    for (const auto& item : sessions.items_) ValidateUtf8(item.name);
    ValidateUtf8(detail);
    raster::TextLayout layout; layout.Wrap(passport_font_16, detail, 192);
    assert(layout.Count() == 6);
    for (unsigned i = 0; i < layout.Count(); ++i)
        assert(raster::TextWidth(passport_font_16, detail + layout.Start(i), detail + layout.End(i)) <= 192);
    Render(argv[1], "sessions");
    const char* labels = "语音菜单会话列表选择会话新建对话返回上一页下一页上/下：选择 OK：确认正在加载创建准备语音请稍候暂无会话未命名音量";
    const char* end = labels + strlen(labels);
    while (labels < end) assert(raster::GlyphId(passport_font_16, raster::NextCodepoint(labels, end)));
    App home; home.page_ = Page::Home; home.calling_ = home.recovering_ = true; home.Publish();
    assert(passport_wifi::input.view == passport_wifi::View::Portrait);
    assert(passport_wifi::PortraitState(passport_wifi::input) == passport_wifi::avatar::State::Waiting);
    passport_wifi::SetAnswer("你好，天依。");
    home.recovering_ = false; home.active_ = true; home.Publish();
    assert(strcmp(passport_wifi::input.answer, "你好，天依。") == 0);
    assert(passport_wifi::PortraitState(passport_wifi::input) == passport_wifi::avatar::State::Listening);
    puts("Passport Chinese menu, UTF-8, portrait and subtitle preservation checks passed");
}
