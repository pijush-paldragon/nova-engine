#include "text/plural.hpp"

#include <algorithm>

namespace nova {
namespace {

std::string_view primary(std::string_view tag) {
    const size_t dash = tag.find_first_of("-_");
    return dash == std::string_view::npos ? tag : tag.substr(0, dash);
}

bool in(std::string_view lang, std::initializer_list<std::string_view> list) {
    return std::find(list.begin(), list.end(), lang) != list.end();
}

} // namespace

PluralCategory pluralCategory(std::string_view tag, i64 value) {
    using P = PluralCategory;
    const std::string_view lang = primary(tag);
    const i64 n = value < 0 ? -value : value;
    const i64 mod10 = n % 10, mod100 = n % 100;
    if (in(lang, {"ja", "zh", "ko", "th", "id", "ms", "vi", "lo", "my", "km"})) return P::Other;
    if (in(lang, {"fr"}) || tag == "pt-BR" || tag == "pt_BR") return n <= 1 ? P::One : P::Other;
    if (in(lang, {"ru", "uk", "be"})) {
        if (mod10 == 1 && mod100 != 11) return P::One;
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return P::Few;
        return P::Many;
    }
    if (in(lang, {"pl"})) {
        if (n == 1) return P::One;
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return P::Few;
        return P::Many;
    }
    if (in(lang, {"cs", "sk"})) {
        if (n == 1) return P::One;
        if (n >= 2 && n <= 4) return P::Few;
        return P::Other;
    }
    if (in(lang, {"ar"})) {
        if (n == 0) return P::Zero;
        if (n == 1) return P::One;
        if (n == 2) return P::Two;
        if (mod100 >= 3 && mod100 <= 10) return P::Few;
        if (mod100 >= 11 && mod100 <= 99) return P::Many;
        return P::Other;
    }
    if (in(lang, {"he"})) {
        if (n == 1) return P::One;
        if (n == 2) return P::Two;
        return P::Other;
    }
    return n == 1 ? P::One : P::Other; // en, de, es, it, pt, nl, sv, tr, el, hi, fa, ur, ...
}

const char* pluralName(PluralCategory c) {
    static const char* names[] = {"zero", "one", "two", "few", "many", "other"};
    return names[size_t(c)];
}

u32 pluralCategoriesOf(std::string_view tag) {
    u32 mask = 0;
    for (i64 n : {0, 1, 2, 3, 5, 11, 21, 22, 100, 101, 111}) mask |= 1u << u32(pluralCategory(tag, n));
    return mask;
}

std::string chooseLanguage(std::span<const std::string> available, std::string_view saved,
                           std::span<const std::string> preferred) {
    auto has = [&](std::string_view tag) {
        return std::find(available.begin(), available.end(), tag) != available.end();
    };
    if (!saved.empty() && has(saved)) return std::string(saved);
    for (const std::string& p : preferred) {
        if (has(p)) return p;
        if (has(primary(p))) return std::string(primary(p));
    }
    return "en";
}

} // namespace nova
