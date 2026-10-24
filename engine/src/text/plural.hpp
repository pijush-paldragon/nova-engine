#pragma once

#include <nova/core/types.hpp>

#include <span>
#include <string>
#include <string_view>

namespace nova {

enum class PluralCategory : u8 { Zero, One, Two, Few, Many, Other };

// CLDR cardinal plural category of an integer in a language (BCP-47 tag; region ignored).
PluralCategory pluralCategory(std::string_view lang, i64 n);
const char* pluralName(PluralCategory c);
// The categories a language uses (for checks), as a bit mask of 1 << category.
u32 pluralCategoriesOf(std::string_view lang);

// Saved choice if available, else the first preferred locale with a file (exact tag, then its
// language part), else "en".
std::string chooseLanguage(std::span<const std::string> available, std::string_view saved,
                           std::span<const std::string> preferred);

} // namespace nova
