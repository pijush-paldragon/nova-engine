#include "text/text_block.hpp"

#include "text/font_runs.hpp"
#include "text/line_break.hpp"
#include "text/text_backend.hpp"

#include <nova/core/alloc_stats.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace nova {
namespace {

constexpr std::string_view kEllipsis = "…";

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

// Strong direction of a piece's text: right-to-left letters (Hebrew, Arabic and their
// presentation forms) win; else any letter or digit makes it left to right; else neutral.
i8 strongDirection(std::string_view s) {
    const char* p = s.data();
    size_t left = s.size();
    i8 dir = 0;
    while (left > 0) {
        const char32_t c = SDL_StepUTF8(&p, &left);
        if ((c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) return -1;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x00C0) dir = 1;
    }
    return dir;
}

// SDL_ttf reads a length of 0 as "null-terminated", so empty views must point at "".
const char* textPtr(std::string_view s) { return s.empty() ? "" : s.data(); }

} // namespace

bool TextBlock::set(const TextBlockParams& p) {
    if (p.fonts.empty()) return false;
    const f32 lineSpacing = p.lineSpacing > 0 ? p.lineSpacing : 1;
    if (laidOut_ && fontGeneration_ == backend_->fontGeneration() && p.text == text_ && p.wrapWidth == wrapWidth_ &&
        p.maxLines == maxLines_ && lineSpacing == lineSpacing_ && p.fitBox == fitBox_ && p.rtl == rtl_ &&
        fonts_ == p.fonts && (p.rich != nullptr) == hadResolver_ && p.richRevision == richRevision_) {
        return true;
    }
    // Reuse capacity: no allocation unless the text or the font list grows.
    text_.assign(p.text.data(), p.text.size());
    fonts_.assign(p.fonts);
    wrapWidth_ = p.wrapWidth;
    maxLines_ = p.maxLines;
    lineSpacing_ = lineSpacing;
    fitBox_ = p.fitBox;
    rtl_ = p.rtl;
    hadResolver_ = p.rich != nullptr;
    richRevision_ = p.richRevision;
    const bool markup = p.rich && text_.find('[') != std::string::npos;
    rich_ = markup || needsPieces();
    if (!rich_) {
        releaseRich();
    } else {
        lineTexts_.clear();
        if (markup) resolveRich(*p.rich);
        else plainRun();
    }
    return layout();
}

bool TextBlock::layout() {
    laidOut_ = false;
    color_ = {0, 0, 0, 0}; // text objects made or re-pointed below take the colour on the next draw
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
    if (rich_) {
        for (size_t i = 0; i < tries; ++i) {
            if (!shapeRich(i)) return false;
            if (fits()) break;
        }
        applyRichEllipsis(fontIndex_);
        backend_->countRebuild();
        laidOut_ = true;
        return true;
    }
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
           (maxLines_ <= 0 || lineCount() <= u32(maxLines_));
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
    if (rich_) {
        if (line >= richLines_.size() || richLines_[line].count == 0) return {};
        const RichLine& l = richLines_[line];
        const u32 begin = pieces_[l.first].begin, end = pieces_[l.first + l.count - 1].end;
        return end > begin ? std::string_view(richText_).substr(begin, end - begin) : std::string_view{};
    }
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
    if (text_.empty() || color.a == 0 || !refresh() || lineCount() == 0) return;
    if (rich_) {
        const ui::TextAlignH a = resolveAlign(align, rtl_);
        const f32 k = a == ui::TextAlignH::Left ? 0 : a == ui::TextAlignH::Center ? 0.5f : 1;
        drawRich((topLeft - Vec2{fit.visible.x, fit.visible.y}) * fit.scale, boxWidth * fit.scale, k, color, scale);
        return;
    }
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

// ---- Rich text ------------------------------------------------------------------------------

void TextBlock::releaseRich() {
    pieceTexts_.clear(); // frees the text objects
    pieces_.clear();
    runs_.clear();
    richLines_.clear();
    placed_.clear();
    richText_.clear();
    richTextLen_ = 0;
}

// Plain text the first font can't draw alone, or in a right-to-left language, is laid out as
// pieces too: each piece is shaped with one font (aligned on a shared baseline) and right-to-left
// lines place words right to left while numbers keep their own order.
bool TextBlock::needsPieces() const {
    if (text_.empty()) return false;
    // Right-to-left letters need word ordering (a one-run text object would reverse numbers in
    // them); text without them ("+10", "Score 20") reads the same in any language.
    if (strongDirection(text_) < 0) return true;
    std::array<TTF_Font*, 8> buf{};
    return !firstFontCovers(backend_->fontChain(fonts_[0], buf), text_);
}

void TextBlock::plainRun() {
    richText_.assign(text_);
    richTextLen_ = u32(richText_.size());
    breakOpportunities(richText_, breaks_);
    const bool shrink = fitBox_.x > 0 || fitBox_.y > 0;
    const size_t steps = shrink ? fonts_.size() : 1;
    runs_.clear();
    richFonts_.clear();
    RichRun r;
    r.end = richTextLen_;
    runs_.push_back(r);
    for (size_t step = 0; step < steps; ++step) richFonts_.push_back(fonts_[step]);
}

// Parses the markup and resolves every run's fonts (one per shrink step), colour and icon.
void TextBlock::resolveRich(const RichResolver& resolver) {
    memory::LoadingScope setup; // changing rich text is setup work: fonts and icons may load
    std::vector<std::string> errors;
    parseMarkup(text_, richText_, markup_, &errors);
    for (const std::string& e : errors) resolver.markupError(e);
    richTextLen_ = u32(richText_.size());
    breakOpportunities(richText_, breaks_);
    runs_.clear();
    richFonts_.clear();
    const bool shrink = fitBox_.x > 0 || fitBox_.y > 0;
    const size_t steps = shrink ? fonts_.size() : 1;
    TTF_Font* nominal = backend_->ttfFont(fonts_[0]);
    const f32 nominalSize = nominal ? f32(TTF_GetFontSize(nominal)) : 0;
    for (const MarkupRun& m : markup_) {
        RichRun r;
        r.begin = u32(m.text.data() - richText_.data());
        r.end = r.begin + u32(m.text.size());
        if (m.kind == MarkupRun::Kind::Icon) {
            r.icon = true;
            r.region = resolver.icon(m.icon);
            r.iconScale = m.iconScale;
        } else {
            r.color = resolver.color(m.style);
        }
        r.fontBase = u32(richFonts_.size());
        for (size_t step = 0; step < steps; ++step) {
            FontHandle f = fonts_[step];
            if (!r.icon && !m.style.plain() && nominalSize > 0) {
                TTF_Font* stepFont = backend_->ttfFont(fonts_[step]);
                const f32 factor = stepFont ? f32(TTF_GetFontSize(stepFont)) / nominalSize : 1;
                if (FontHandle styled = resolver.font(m.style, factor); backend_->ttfFont(styled)) f = styled;
            }
            richFonts_.push_back(f);
        }
        runs_.push_back(r);
    }
}

FontHandle TextBlock::pieceFont(size_t i) const {
    if (i >= pieces_.size()) return {};
    const RichRun& r = runs_[pieces_[i].run];
    return richFonts_[r.fontBase + std::min(fontIndex_, richFonts_.size() - r.fontBase - 1)];
}

// Shapes one text piece with its run's font for a shrink step; icons are sized off the base font.
bool TextBlock::measurePiece(size_t i, size_t step) {
    Piece& p = pieces_[i];
    const RichRun& r = runs_[p.run];
    TTF_Font* font = p.font ? p.font : backend_->ttfFont(richFonts_[r.fontBase + step]);
    if (!font) return false;
    if (p.icon) {
        const f32 h = f32(TTF_GetFontHeight(font)) * r.iconScale;
        const f32 aspect = r.region.src.w > 0 && r.region.src.h > 0 ? r.region.src.w / r.region.src.h : 1;
        p.width = p.advance = h * aspect;
        p.ascent = h;
        p.descent = 0;
        return true;
    }
    // The piece draws its visible text; trailing spaces only add to its advance.
    u32 visible = p.end;
    while (visible > p.begin && (richText_[visible - 1] == ' ' || richText_[visible - 1] == '\t' ||
                                 richText_[visible - 1] == '\n' || richText_[visible - 1] == '\r')) {
        if (richText_[visible - 1] == '\n') p.newline = true;
        --visible;
    }
    const std::string_view text(richText_.data() + p.begin, visible - p.begin);
    const std::string_view spaces(richText_.data() + visible, p.end - visible);
    memory::DriverAllocScope driver; // SDL_ttf text objects and layout
    if (pieceTexts_.size() <= i) pieceTexts_.resize(i + 1);
    if (!pieceTexts_[i]) {
        pieceTexts_[i] = backend_->createText(font, text);
        if (!pieceTexts_[i]) return false;
    } else {
        TTF_SetTextFont(pieceTexts_[i].get(), font);
        TTF_SetTextString(pieceTexts_[i].get(), textPtr(text), text.size());
    }
    int w = 0, h = 0;
    if (!text.empty()) TTF_GetTextSize(pieceTexts_[i].get(), &w, &h);
    int sw = 0;
    if (!p.newline && !spaces.empty()) TTF_GetStringSize(font, spaces.data(), spaces.size(), &sw, nullptr);
    p.width = f32(w);
    p.advance = f32(w + sw);
    p.ascent = f32(TTF_GetFontAscent(font));
    p.descent = f32(-TTF_GetFontDescent(font));
    p.dir = strongDirection(text);
    return true;
}

bool TextBlock::shapeRich(size_t step) {
    TTF_Font* base = backend_->ttfFont(fonts_[step]);
    if (!base) return false;
    fontIndex_ = step;
    richText_.resize(richTextLen_);  // drop an earlier ellipsis
    pieces_.clear();
    for (u32 ri = 0; ri < runs_.size(); ++ri) {
        const RichRun& r = runs_[ri];
        if (r.icon) {
            Piece p;
            p.run = ri;
            p.icon = true;
            p.breakBefore = true;
            pieces_.push_back(p);
            continue;
        }
        // Split the run at break opportunities, then where the font changes; a piece that doesn't
        // start at a break (a style or font change inside a word) stays on the line of the one before.
        std::array<TTF_Font*, 8> buf{};
        const std::span<TTF_Font* const> chain = backend_->fontChain(richFonts_[r.fontBase + step], buf);
        for (u32 pos = r.begin; pos < r.end;) {
            auto next = std::upper_bound(breaks_.begin(), breaks_.end(), pos);
            const u32 end = next != breaks_.end() && *next < r.end ? *next : r.end;
            const bool breakBefore = pos == 0 || std::binary_search(breaks_.begin(), breaks_.end(), pos) ||
                                     (!pieces_.empty() && pieces_.back().icon);
            forEachFontRun(chain, std::string_view(richText_).substr(pos, end - pos), [&](size_t b, size_t e, TTF_Font* f) {
                Piece p;
                p.run = ri;
                p.begin = pos + u32(b);
                p.end = pos + u32(e);
                p.font = f;
                p.breakBefore = b == 0 && breakBefore;
                pieces_.push_back(p);
            });
            pos = end;
        }
    }
    if (pieceTexts_.size() > pieces_.size() + 1) {
        memory::DriverAllocScope driver; // frees text objects
        pieceTexts_.resize(pieces_.size() + 1); // +1: an ellipsis
    }
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (!measurePiece(i, step)) return false;
    }
    placeRichLines(f32(TTF_GetFontAscent(base)), f32(-TTF_GetFontDescent(base)), f32(TTF_GetFontHeight(base)),
                   f32(TTF_GetFontLineSkip(base)));
    return true;
}

// Greedy line filling over words (a run of pieces with no break opportunity between them).
void TextBlock::placeRichLines(f32 baseAscent, f32 baseDescent, f32 baseHeight, f32 baseSkip) {
    const f32 wrapPx = wrapWidth_ > 0 ? wrapWidth_ * backend_->pixelScale() : 0;
    richLines_.clear();
    RichLine line;
    f32 x = 0;
    auto finish = [&] {
        line.ascent = baseAscent;
        line.descent = baseDescent;
        line.width = 0;
        for (u32 k = line.first; k < line.first + line.count; ++k) {
            line.ascent = std::max(line.ascent, pieces_[k].ascent);
            line.descent = std::max(line.descent, pieces_[k].descent);
            line.width = std::max(line.width, pieces_[k].x + pieces_[k].width);
        }
        richLines_.push_back(line);
        orderLine(richLines_.back());
        line = RichLine{};
        x = 0;
    };
    for (size_t i = 0; i < pieces_.size();) {
        size_t j = i + 1;
        while (j < pieces_.size() && !pieces_[j].breakBefore) ++j;
        f32 fit = 0;
        for (size_t k = i; k < j; ++k) fit += k + 1 < j ? pieces_[k].advance : pieces_[k].width;
        if (wrapPx > 0 && line.count > 0 && x + fit > wrapPx + 0.5f) {
            finish();
            line.first = u32(i);
        }
        if (line.count == 0) line.first = u32(i);
        for (size_t k = i; k < j; ++k) {
            pieces_[k].x = x;
            pieces_[k].line = u32(richLines_.size());
            x += pieces_[k].advance;
            ++line.count;
        }
        if (pieces_[j - 1].newline) {
            finish();
            line.first = u32(j);
        }
        i = j;
    }
    if (line.count > 0 || richLines_.empty()) finish();
    stackRichLines(baseHeight, baseSkip);
}

// Where each piece of a line is drawn. Left-to-right lines keep reading order. Right-to-left
// lines run right to left, except that a run of left-to-right pieces (a number, a Latin or CJK
// phrase; neutral pieces between two such pieces join it) keeps its own order: piece-level bidi.
void TextBlock::orderLine(const RichLine& line) {
    const u32 end = line.first + line.count;
    if (!rtl_) {
        for (u32 k = line.first; k < end; ++k) pieces_[k].vx = pieces_[k].x;
        return;
    }
    auto ltrAt = [&](u32 k) {
        if (pieces_[k].dir != 0) return pieces_[k].dir > 0;
        u32 before = k, after = k;
        while (before > line.first && pieces_[before - 1].dir == 0) --before;
        while (after + 1 < end && pieces_[after + 1].dir == 0) ++after;
        return before > line.first && after + 1 < end && pieces_[before - 1].dir > 0 && pieces_[after + 1].dir > 0;
    };
    for (u32 k = line.first; k < end;) {
        if (!ltrAt(k)) {
            pieces_[k].vx = line.width - pieces_[k].x - pieces_[k].width;
            ++k;
            continue;
        }
        u32 last = k;
        while (last + 1 < end && ltrAt(last + 1)) ++last;
        // The run's span, mirrored as a whole, then laid out left to right inside it.
        const f32 left = line.width - (pieces_[last].x + pieces_[last].width);
        for (u32 j = k; j <= last; ++j) pieces_[j].vx = left + (pieces_[j].x - pieces_[k].x);
        k = last + 1;
    }
}

// Vertical placement: each line as tall as its tallest piece (at least the base font's line).
void TextBlock::stackRichLines(f32 baseHeight, f32 baseSkip) {
    f32 y = 0, widest = 0, bottom = 0;
    for (RichLine& l : richLines_) {
        l.y = y;
        bottom = y + std::max(baseHeight, l.ascent + l.descent);
        y += std::max(baseSkip, l.ascent + l.descent) * lineSpacing_;
        widest = std::max(widest, l.width);
    }
    lineStepPx_ = baseSkip * lineSpacing_;
    sizePx_ = {widest, bottom};
}

// Keeps maxLines lines and ends the last with "…", dropping trailing pieces until it fits.
void TextBlock::applyRichEllipsis(size_t step) {
    if (maxLines_ <= 0 || richLines_.size() <= size_t(maxLines_)) return;
    TTF_Font* base = backend_->ttfFont(fonts_[step]);
    if (!base) return;
    RichLine& last = richLines_[size_t(maxLines_ - 1)];
    pieces_.resize(last.first + last.count);
    richLines_.resize(size_t(maxLines_));
    // The ellipsis takes the look of the last text on the line (else of the first text run).
    u32 run = u32(runs_.size());
    for (size_t k = pieces_.size(); k-- > last.first && run == runs_.size();) {
        if (!pieces_[k].icon) run = pieces_[k].run;
    }
    for (u32 ri = 0; ri < runs_.size() && run == runs_.size(); ++ri) {
        if (!runs_[ri].icon) run = ri;
    }
    if (run == runs_.size()) return;
    Piece dots;
    dots.run = run;
    dots.begin = u32(richText_.size());
    richText_.append(kEllipsis);
    dots.end = u32(richText_.size());
    dots.line = u32(maxLines_ - 1);
    std::array<TTF_Font*, 8> buf{};
    dots.font = fontFor(backend_->fontChain(richFonts_[runs_[run].fontBase + step], buf), U'…');
    pieces_.push_back(dots);
    if (!measurePiece(pieces_.size() - 1, step)) {
        pieces_.pop_back();
        return;
    }
    const f32 dotsWidth = pieces_.back().width;
    const f32 wrapPx = wrapWidth_ > 0 ? wrapWidth_ * backend_->pixelScale() : 0;
    // Drop the line's trailing pieces (and their text objects, which pair with pieces by index)
    // until the ellipsis fits after them.
    while (last.count > 0 && wrapPx > 0) {
        const size_t tail = last.first + last.count - 1;
        if (pieces_[tail].x + pieces_[tail].width + dotsWidth <= wrapPx + 0.5f) break;
        pieces_.erase(pieces_.begin() + long(tail));
        memory::DriverAllocScope driver; // frees a text object
        pieceTexts_.erase(pieceTexts_.begin() + long(tail));
        --last.count;
    }
    Piece& d = pieces_.back();
    d.x = last.count > 0 ? pieces_[last.first + last.count - 1].x + pieces_[last.first + last.count - 1].width : 0;
    ++last.count;
    last.width = d.x + d.width;
    last.ascent = std::max(last.ascent, d.ascent);
    last.descent = std::max(last.descent, d.descent);
    orderLine(last);
    stackRichLines(f32(TTF_GetFontHeight(base)), f32(TTF_GetFontLineSkip(base)));
}

std::string_view TextBlock::pieceText(size_t i) const {
    if (i >= pieces_.size() || pieces_[i].icon) return {};
    return std::string_view(richText_).substr(pieces_[i].begin, pieces_[i].end - pieces_[i].begin);
}

Rect TextBlock::pieceBox(size_t i, f32 boxWidth, ui::TextAlignH align) const {
    if (i >= pieces_.size()) return {};
    const f32 ps = backend_->pixelScale();
    const ui::TextAlignH a = resolveAlign(align, rtl_);
    const f32 k = a == ui::TextAlignH::Left ? 0 : a == ui::TextAlignH::Center ? 0.5f : 1;
    const Piece& p = pieces_[i];
    const RichLine& line = richLines_[p.line];
    const f32 x = (boxWidth * ps - line.width) * k + p.vx;
    const f32 y = line.y + line.ascent - p.ascent;
    return {x / ps, y / ps, p.width / ps, (p.ascent + p.descent) / ps};
}

std::span<const PlacedIcon> TextBlock::icons(f32 boxWidth, ui::TextAlignH align) {
    placed_.clear();
    if (!rich_ || !refresh()) return {};
    const f32 ps = backend_->pixelScale();
    for (size_t i = 0; i < pieces_.size(); ++i) {
        if (!pieces_[i].icon) continue;
        Rect box = pieceBox(i, boxWidth, align);
        box.h = pieces_[i].ascent / ps;
        placed_.push_back({runs_[pieces_[i].run].region, box});
    }
    return placed_;
}

void TextBlock::drawRich(Vec2 origin, f32 boxPx, f32 k, Color color, f32 scale) {
    if (!(color == color_)) {
        for (size_t i = 0; i < pieces_.size(); ++i) {
            if (pieces_[i].icon || i >= pieceTexts_.size() || !pieceTexts_[i]) continue;
            Color c = runs_[pieces_[i].run].color.value_or(color);
            if (runs_[pieces_[i].run].color) c.a = u8((u32(c.a) * u32(color.a) + 127) / 255);
            TTF_SetTextColor(pieceTexts_[i].get(), c.r, c.g, c.b, c.a);
        }
        color_ = color;
    }
    SDL_Renderer* renderer = backend_->renderer();
    memory::DriverAllocScope driver;
    if (scale != 1) SDL_SetRenderScale(renderer, scale, scale);
    for (size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& p = pieces_[i];
        if (p.icon || p.width <= 0 || i >= pieceTexts_.size() || !pieceTexts_[i]) continue;
        const RichLine& line = richLines_[p.line];
        const f32 x = origin.x + ((boxPx - line.width) * k + p.vx) * scale;
        const f32 y = origin.y + (line.y + line.ascent - p.ascent) * scale;
        if (scale == 1) TTF_DrawRendererText(pieceTexts_[i].get(), std::round(x), std::round(y));
        else TTF_DrawRendererText(pieceTexts_[i].get(), x / scale, y / scale);
    }
    if (scale != 1) SDL_SetRenderScale(renderer, 1, 1);
}

} // namespace nova
