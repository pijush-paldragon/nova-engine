#pragma once

// Splits text into runs that each draw with one font of a fallback chain. SDL_ttf places every
// glyph at its own font's ascent, so a text object mixing fonts with different ascents has
// glyphs off the baseline; TextBlock shapes each run alone and aligns runs itself.

#include <nova/core/types.hpp>

#include <SDL3/SDL_stdinc.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <span>
#include <string_view>

namespace nova {

// Spaces and punctuation stay in the font of the text around them when it has them. Digits are
// not neutral: a number takes the first font that has digits, so inside right-to-left text it is
// shaped on its own (left to right) instead of reversed with the letters around it.
inline bool neutralChar(char32_t c) {
    const bool letterOrDigit = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    return (c < 0x80 && !letterOrDigit) || (c >= 0x2000 && c <= 0x206F) || c == 0x00A0;
}

// The first font of the chain that has the character (chain[0] if none does).
inline TTF_Font* fontFor(std::span<TTF_Font* const> chain, char32_t c) {
    for (TTF_Font* f : chain) {
        if (TTF_FontHasGlyph(f, c)) return f;
    }
    return chain.empty() ? nullptr : chain[0];
}

// True if the chain's first font draws every character of `text` itself.
inline bool firstFontCovers(std::span<TTF_Font* const> chain, std::string_view text) {
    if (chain.empty()) return true;
    const char* p = text.data();
    size_t left = text.size();
    while (left > 0) {
        const char32_t c = SDL_StepUTF8(&p, &left);
        if (c != '\n' && c != '\r' && c != '\t' && !TTF_FontHasGlyph(chain[0], c)) return false;
    }
    return true;
}

// Calls fn(begin, end, font) for each run of `text` (byte offsets) drawn with one font.
template <class F>
void forEachFontRun(std::span<TTF_Font* const> chain, std::string_view text, F&& fn) {
    const char* p = text.data();
    size_t left = text.size();
    size_t runStart = 0;
    TTF_Font* current = nullptr;
    while (left > 0) {
        const size_t at = size_t(p - text.data());
        const char32_t c = SDL_StepUTF8(&p, &left);
        TTF_Font* f = current && neutralChar(c) && TTF_FontHasGlyph(current, c) ? current : fontFor(chain, c);
        if (!current && neutralChar(c)) {
            // Leading punctuation ("(" before a word) joins the font of the letter after it, so it
            // is shaped with that word (and mirrored in right-to-left text).
            const char* q = p;
            size_t qleft = left;
            while (qleft > 0) {
                const char32_t next = SDL_StepUTF8(&q, &qleft);
                if (neutralChar(next)) continue;
                if (TTF_Font* nf = fontFor(chain, next); TTF_FontHasGlyph(nf, c)) f = nf;
                break;
            }
        }
        if (current && f != current) {
            fn(runStart, at, current);
            runStart = at;
        }
        current = f;
    }
    if (current) fn(runStart, text.size(), current);
}

} // namespace nova
