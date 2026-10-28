// Adapted from deota-jonah/ai-passport eaee36b (MIT); see README provenance.
#include "passport_language.h"
#include <atomic>
#include <cstddef>
namespace passport_wifi {
namespace {
std::atomic<Language> current{Language::Chinese};
struct Pair { const char* zh; const char* en; };
constexpr Pair text[] = {
#define PASSPORT_TEXT(id, zh, en) {zh, en},
#include "passport_messages.inc"
#undef PASSPORT_TEXT
};
static_assert(sizeof text / sizeof *text == static_cast<size_t>(TextId::Count));
}
Language GetLanguage() { return current.load(); }
void SetLanguage(Language language) { current.store(StoredLanguage(static_cast<uint8_t>(language))); }
const char* Translate(TextId id, Language language) {
    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(TextId::Count)) return "";
    return language == Language::English ? text[index].en : text[index].zh;
}
}
