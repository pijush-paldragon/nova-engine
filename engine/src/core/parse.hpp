#pragma once

#include <nova/core/types.hpp>

#include <string_view>

namespace nova {

// Parses the whole string as a floating-point number (no leading/trailing junk), independent of
// the C locale. Used instead of floating-point std::from_chars, which needs iOS 26+.
bool parseFloat(std::string_view text, f64& out);
bool parseFloat(std::string_view text, f32& out);

} // namespace nova
