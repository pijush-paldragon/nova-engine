#include "core/parse.hpp"

#include <SDL3/SDL_stdinc.h>

#include <cstring>

namespace nova {

bool parseFloat(std::string_view text, f64& out) {
    char buffer[64];
    if (text.empty() || text.size() >= sizeof buffer) return false;
    const char first = text.front();
    if (!(first == '-' || first == '+' || first == '.' || (first >= '0' && first <= '9'))) return false;
    std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
    char* end = nullptr;
    f64 value = SDL_strtod(buffer, &end); // SDL's implementation ignores the C locale
    if (end != buffer + text.size()) return false;
    out = value;
    return true;
}

bool parseFloat(std::string_view text, f32& out) {
    f64 value = 0;
    if (!parseFloat(text, value)) return false;
    out = f32(value);
    return true;
}

} // namespace nova
