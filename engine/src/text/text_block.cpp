#include "text/text_block.hpp"

#include "text/text_backend.hpp"

#include <nova/core/alloc_stats.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>

namespace nova {
namespace {

constexpr std::string_view kEllipsis = "…";
// Kinsoku: characters a line may not start with, and ones it may not end with.
constexpr std::u32string_view kNoLineStart = U"。、，．・：；？！）」』】〕ー";
constexpr std::u32string_view kNoLineEnd = U"（「『【〔";

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char32_t firstCodepoint(std::string_view s) {
    const char* p = s.data();
    size_t n = s.size();
    return SDL_StepUTF8(&p, &n);
}

// Byte offset where the last codepoint of a non-empty string starts.
size_t lastCodepointStart(std::string_view s) {
    if (s.empty()) return 0;
    size_t i = s.size() - 1;
    while (i > 0 && (u8(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

char32_t lastCodepoint(std::string_view s) { return s.empty() ? 0 : firstCodepoint(s.substr(lastCodepointStart(s))); }

// SDL_ttf reads a length of 0 as "null-terminated", so empty views must point at "".
const char* textPtr(std::string_view s) { return s.empty() ? "" : s.data(); }

} // namespace

bool TextBlock::set(const TextBlockParams& p) {
    if (p.fonts.empty()) return false;
    if (laidOut_ && fontGeneration_ == backend_->fontGeneration() && p.text == text_ && p.wrapWidth == wrapWidth_ &&
        p.maxLines == maxLines_ && p.lineSpacing == lineSpacing_ && p.fitBox == fitBox_ && p.rtl == rtl_ &&
        fonts_ == p.fonts) {
        return true;
    }
    // Reuse capacity: no allocation unless the text or the font list grows.
    text_.assign(p.text.data(), p.text.size());
    fonts_.assign(p.fonts);
    wrapWidth_ = p.wrapWidth;
    maxLines_ = p.maxLines;
    lineSpacing_ = p.lineSpacing > 0 ? p.lineSpacing : 1;
    fitBox_ = p.fitBox;
    rtl_ = p.rtl;
    return layout();
}

bool TextBlock::layout() {
    laidOut_ = false;
    fontGeneration_ = backend_->fontGeneration();
    lines_.clear();
    linesDirty_ = true;
    fontIndex_ = 0;
    sizePx_ = {};
    if (text_.empty()) {
        shown_.clear();
        laidOut_ = true;
        return true;
    }
    // Shrink: the largest font whose text fits, else the smallest (then truncated as usual).
    const bool shrink = fitBox_.x > 0 || fitBox_.y > 0;
    const size_t tries = shrink ? fonts_.size() : 1;
    for (size_t i = 0; i < tries; ++i) {
        shown_.assign(text_);
        if (!shape(i)) return false;
        applyKinsoku();
        if (fits()) break;
    }
    applyEllipsis();
    backend_->countRebuild();
    laidOut_ = true;
    return true;
}

bool TextBlock::shape(size_t index) {
    TTF_Font* font = backend_->ttfFont(fonts_[index]);
    if (!font) return false;
    fontIndex_ = index;
    linesDirty_ = true;
    const f32 ps = backend_->pixelScale();
    memory::DriverAllocScope driver; // SDL_ttf layout
    if (!paragraph_) {
        paragraph_ = backend_->createText(font, shown_);
        if (!paragraph_) return false;
    } else {
        TTF_SetTextFont(paragraph_.get(), font);
        TTF_SetTextString(paragraph_.get(), textPtr(shown_), shown_.size());
    }
    TTF_SetTextWrapWidth(paragraph_.get(), wrapWidth_ > 0 ? std::max(1, int(std::floor(wrapWidth_ * ps))) : 0);
    int w = 0, h = 0;
    TTF_GetTextSize(paragraph_.get(), &w, &h);
    const int n = std::max(paragraph_->num_lines, 1);
    const f32 skip = f32(TTF_GetFontLineSkip(font));
    lineStepPx_ = skip * lineSpacing_;
    lines_.resize(size_t(n));
    f32 widest = 0;
    for (int i = 0; i < n; ++i) {
        Line& line = lines_[size_t(i)];
        TTF_SubString sub{};
        if (n == 1 || !TTF_GetTextSubStringForLine(paragraph_.get(), i, &sub)) {
            sub.offset = 0;
            sub.length = int(shown_.size());
        }
        u32 offset = u32(std::clamp(sub.offset, 0, int(shown_.size())));
        u32 length = u32(std::clamp(sub.length, 0, int(shown_.size() - offset)));
        while (length > 0 && isSpace(shown_[offset + length - 1])) --length;
        while (length > 0 && (shown_[offset] == ' ' || shown_[offset] == '\t')) ++offset, --length;
        line = {offset, length, 0};
        if (n == 1) {
            line.width = f32(w);
        } else if (length > 0) {
            int lw = 0;
            TTF_GetStringSize(font, shown_.data() + offset, length, &lw, nullptr);
            line.width = f32(lw);
        }
        widest = std::max(widest, line.width);
    }
    const f32 firstRow = f32(h) - skip * f32(n - 1);
    sizePx_ = {widest, firstRow + lineStepPx_ * f32(n - 1)};
    return true;
}

bool TextBlock::fits() const {
    const Vec2 s = sizePx_ / backend_->pixelScale();
    return (fitBox_.x <= 0 || s.x <= fitBox_.x + 0.5f) && (fitBox_.y <= 0 || s.y <= fitBox_.y + 0.5f) &&
           (maxLines_ <= 0 || lines_.size() <= size_t(maxLines_));
}

// Moves a character down a line where a break would leave forbidden punctuation at a line's
// start or end. Setup-time only (it edits a copy and re-lays out).
void TextBlock::applyKinsoku() {
    if (wrapWidth_ <= 0 || std::all_of(shown_.begin(), shown_.end(), [](char c) { return u8(c) < 0x80; })) return;
    for (int pass = 0; pass < 16; ++pass) {
        size_t insertAt = std::string::npos;
        for (size_t i = 0; i < lines_.size() && insertAt == std::string::npos; ++i) {
            const std::string_view line = lineText(u32(i));
            if (line.empty()) continue;
            if (i > 0 && kNoLineStart.find(firstCodepoint(line)) != std::u32string_view::npos) {
                const std::string_view prev = lineText(u32(i - 1));
                if (size_t k = lastCodepointStart(prev); k > 0) insertAt = lines_[i - 1].offset + k;
            } else if (i + 1 < lines_.size() && kNoLineEnd.find(lastCodepoint(line)) != std::u32string_view::npos) {
                if (size_t k = lastCodepointStart(line); k > 0) insertAt = lines_[i].offset + k;
            }
        }
        if (insertAt == std::string::npos) return;
        shown_.insert(insertAt, 1, '\n');
        if (!shape(fontIndex_)) return;
    }
}

// Cuts the text after maxLines lines and ends the last kept line with "…".
void TextBlock::applyEllipsis() {
    if (maxLines_ <= 0 || lines_.size() <= size_t(maxLines_)) return;
    const Line last = lines_[size_t(maxLines_ - 1)];
    shown_.resize(last.offset + last.length);
    for (;;) {
        while (!shown_.empty() && isSpace(shown_.back())) shown_.pop_back();
        shown_.append(kEllipsis);
        if (!shape(fontIndex_) || lines_.size() <= size_t(maxLines_) || shown_.size() <= kEllipsis.size()) return;
        shown_.resize(shown_.size() - kEllipsis.size());
        shown_.resize(lastCodepointStart(shown_));
    }
}

bool TextBlock::refresh() {
    if (!laidOut_) return false;
    if (fontGeneration_ != backend_->fontGeneration()) return layout();
    return true;
}

std::string_view TextBlock::lineText(u32 line) const {
    if (line >= lines_.size()) return {};
    return std::string_view(shown_).substr(lines_[line].offset, lines_[line].length);
}

Vec2 TextBlock::size() {
    if (!refresh()) return {};
    return sizePx_ / backend_->pixelScale();
}

void TextBlock::buildLineTexts() {
    TTF_Font* font = backend_->ttfFont(fonts_[fontIndex_]);
    memory::DriverAllocScope driver; // SDL_ttf layout
    if (lineTexts_.size() < lines_.size()) lineTexts_.resize(lines_.size());
    for (size_t i = 0; i < lines_.size(); ++i) {
        const std::string_view s = lineText(u32(i));
        if (!lineTexts_[i]) {
            lineTexts_[i] = backend_->createText(font, s);
        } else {
            TTF_SetTextFont(lineTexts_[i].get(), font);
            TTF_SetTextString(lineTexts_[i].get(), textPtr(s), s.size());
        }
    }
    linesDirty_ = false;
    color_ = {0, 0, 0, 0}; // new line texts take the colour on the next draw
}

void TextBlock::draw(Vec2 topLeft, f32 boxWidth, ui::TextAlignH align, const ScreenFit& fit, Color color, f32 scale) {
    if (text_.empty() || color.a == 0 || !refresh() || lines_.empty()) return;
    const bool multi = lines_.size() > 1;
    if (multi && linesDirty_) buildLineTexts();
    if (!(color == color_)) {
        TTF_SetTextColor(paragraph_.get(), color.r, color.g, color.b, color.a);
        for (const TtfTextPtr& t : lineTexts_) {
            if (t) TTF_SetTextColor(t.get(), color.r, color.g, color.b, color.a);
        }
        color_ = color;
    }
    const ui::TextAlignH a = resolveAlign(align, rtl_);
    const f32 k = a == ui::TextAlignH::Left ? 0 : a == ui::TextAlignH::Center ? 0.5f : 1;
    const Vec2 origin = (topLeft - Vec2{fit.visible.x, fit.visible.y}) * fit.scale;
    const f32 boxPx = boxWidth * fit.scale;
    SDL_Renderer* renderer = backend_->renderer();
    memory::DriverAllocScope driver;
    if (scale != 1) SDL_SetRenderScale(renderer, scale, scale);
    const size_t count = multi ? lines_.size() : 1;
    for (size_t i = 0; i < count; ++i) {
        TTF_Text* t = multi ? lineTexts_[i].get() : paragraph_.get();
        if (!t || lines_[i].length == 0) continue;
        const f32 x = origin.x + (boxPx - lines_[i].width) * k * scale;
        const f32 y = origin.y + lineStepPx_ * f32(i) * scale;
        if (scale == 1) TTF_DrawRendererText(t, std::round(x), std::round(y));
        else TTF_DrawRendererText(t, x / scale, y / scale);
    }
    if (scale != 1) SDL_SetRenderScale(renderer, 1, 1);
}

} // namespace nova
