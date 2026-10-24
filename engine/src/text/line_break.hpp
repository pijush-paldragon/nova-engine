#pragma once

#include <nova/core/types.hpp>

#include <string_view>
#include <vector>

namespace nova {

// Kinsoku: characters a line may not start with, and ones it may not end with.
inline constexpr std::u32string_view kNoLineStart = U"。、，．・：；？！）」』】〕ー";
inline constexpr std::u32string_view kNoLineEnd = U"（「『【〔";

// Han, kana, hangul and CJK/fullwidth punctuation: text a line may break between any two of.
bool isCjk(char32_t c);

// Byte offsets (in increasing order, never 0 or utf8.size()) where a new line may start: after
// spaces and newlines, after - – —, and before or after CJK characters; never before a
// kNoLineStart character or after a kNoLineEnd one.
void breakOpportunities(std::string_view utf8, std::vector<u32>& out);

} // namespace nova
