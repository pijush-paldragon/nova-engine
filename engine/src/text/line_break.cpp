#include "text/line_break.hpp"

#include <SDL3/SDL_stdinc.h>

namespace nova {

bool isCjk(char32_t c) {
    return (c >= 0x3000 && c <= 0x30FF) ||  // CJK punctuation, hiragana, katakana
           (c >= 0x3400 && c <= 0x4DBF) ||  // Han extension A
           (c >= 0x4E00 && c <= 0x9FFF) ||  // Han
           (c >= 0xAC00 && c <= 0xD7AF) ||  // Hangul syllables
           (c >= 0xF900 && c <= 0xFAFF) ||  // Han compatibility
           (c >= 0xFF00 && c <= 0xFFEF) ||  // fullwidth forms
           (c >= 0x20000 && c <= 0x2FFFF);  // Han extensions B+
}

void breakOpportunities(std::string_view utf8, std::vector<u32>& out) {
    out.clear();
    const char* p = utf8.data();
    size_t left = utf8.size();
    char32_t prev = 0;
    while (left > 0) {
        const u32 at = u32(p - utf8.data());
        const char32_t c = SDL_StepUTF8(&p, &left);
        if (at > 0) {
            const bool afterSpace = (prev == ' ' || prev == '\t' || prev == '\n') && c != ' ' && c != '\t' && c != '\n';
            const bool afterDash = (prev == '-' || prev == U'–' || prev == U'—') && c != ' ' && c != '\n';
            const bool cjk = (isCjk(prev) || isCjk(c)) && prev != ' ' && c != ' ';
            const bool forbidden = kNoLineStart.find(c) != std::u32string_view::npos ||
                                   kNoLineEnd.find(prev) != std::u32string_view::npos;
            if (prev == '\n' || ((afterSpace || afterDash || cjk) && !forbidden)) out.push_back(at);
        }
        prev = c;
    }
}

} // namespace nova
