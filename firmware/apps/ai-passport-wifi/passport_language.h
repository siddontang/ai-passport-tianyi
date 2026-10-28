// Adapted from deota-jonah/ai-passport eaee36b (MIT); see README provenance.
#pragma once
#include <cstdint>
namespace passport_wifi {
enum class Language : uint8_t { Chinese, English };
enum class TextId {
#define PASSPORT_TEXT(id, zh, en) id,
#include "passport_messages.inc"
#undef PASSPORT_TEXT
    Count
};
constexpr Language StoredLanguage(uint8_t value) { return value == 1 ? Language::English : Language::Chinese; }
Language GetLanguage();
void SetLanguage(Language language);
const char* Translate(TextId id, Language language);
inline const char* Tr(TextId id) { return Translate(id, GetLanguage()); }
}
