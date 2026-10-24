#pragma once

#include "text/markup.hpp"

#include <nova/core/math.hpp>
#include <nova/render/camera.hpp>
#include <nova/render/types.hpp>
#include <nova/text/text.hpp>
#include <nova/ui/types.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct TTF_Text;
struct TTF_Font;

namespace nova {

class TextBackend;

struct TtfTextDeleter {
    void operator()(TTF_Text* text) const; // text.cpp (keeps the live-text count)
};
using TtfTextPtr = std::unique_ptr<TTF_Text, TtfTextDeleter>;

// `Start`/`End` resolved for a paragraph direction; Left/Center/Right stay as they are.
constexpr ui::TextAlignH resolveAlign(ui::TextAlignH align, bool rtl) {
    if (align == ui::TextAlignH::Start) return rtl ? ui::TextAlignH::Right : ui::TextAlignH::Left;
    if (align == ui::TextAlignH::End) return rtl ? ui::TextAlignH::Left : ui::TextAlignH::Right;
    return align;
}

// Fonts, colours and icons for rich text; the UI implements it from the theme and assets.
class RichResolver {
public:
    // The font for a styled run; `sizeFactor` is the shrink step's size over the nominal size.
    virtual FontHandle font(const RunStyle& style, f32 sizeFactor) const = 0;
    // The run's colour; nullopt = the colour given to draw().
    virtual std::optional<Color> color(const RunStyle& style) const = 0;
    virtual SpriteRegion icon(std::string_view name) const = 0;
    virtual void markupError(const std::string& message) const = 0;

protected:
    ~RichResolver() = default;
};

// An inline icon of rich text: `box` is in design units from the block's top-left.
struct PlacedIcon {
    SpriteRegion region;
    Rect box;
};

struct TextBlockParams {
    // [0] is the nominal size; with a fitBox, further entries are the shrink steps, largest first.
    std::span<const FontHandle> fonts;
    std::string_view text;
    f32 wrapWidth = 0;    // design units; 0 = break only at newlines
    i32 maxLines = 0;     // 0 = no limit; overflow ends with "…"
    f32 lineSpacing = 1;
    Vec2 fitBox{};        // non-zero: pick the largest font whose text fits (0 on an axis = unbounded)
    bool rtl = false;     // paragraph direction: Start/End alignment, rich runs' order
    // Non-null: text containing "[" is parsed as markup. Used during set() only. Bump
    // richRevision when what it resolves to changes (a new theme).
    const RichResolver* rich = nullptr;
    u32 richRevision = 0;
};

// A paragraph of text: wrapped, truncated to maxLines, shrunk to fit and aligned per line. Plain
// text is shaped as one SDL_ttf text; rich text as one per word piece, with inline icons.
// Re-lays out only when an input, a font or the pixel scale changed; drawing and unchanged
// set() calls never allocate (changing rich text is setup work: it re-parses).
class TextBlock {
public:
    explicit TextBlock(TextBackend& backend) : backend_(&backend) {}
    TextBlock(TextBlock&&) noexcept = default;
    TextBlock& operator=(TextBlock&&) noexcept = default;

    // False if the font is gone or the text could not be created.
    bool set(const TextBlockParams& p);
    // Size in design units ({0, 0} for empty text).
    Vec2 size();
    u32 lineCount() const { return u32(rich_ ? richLines_.size() : lines_.size()); }
    size_t fontIndex() const { return fontIndex_; }
    bool empty() const { return text_.empty(); }
    // Laid out as word pieces: markup, a right-to-left language, or glyphs from fallback fonts.
    bool rich() const { return rich_; }
    // The text as laid out (plain: with inserted line breaks and the ellipsis; rich: the visible
    // text without tags), and one line of plain text.
    std::string_view shownText() const { return rich_ ? std::string_view(richText_) : std::string_view(shown_); }
    std::string_view lineText(u32 line) const;
    // Rich text's icons as placed by a draw with this box width and alignment; empty for plain text.
    std::span<const PlacedIcon> icons(f32 boxWidth, ui::TextAlignH align);
    // Pieces (tests): the run font each is shaped with, its text, and where a draw with this box
    // width and alignment puts it (design units from the block's top-left).
    size_t pieceCount() const { return pieces_.size(); }
    FontHandle pieceFont(size_t i) const;
    std::string_view pieceText(size_t i) const;
    Rect pieceBox(size_t i, f32 boxWidth, ui::TextAlignH align) const;

    // Draws with its top-left at `topLeft` (design units), each line aligned within `boxWidth`,
    // scaled about `topLeft`.
    void draw(Vec2 topLeft, f32 boxWidth, ui::TextAlignH align, const ScreenFit& fit, Color color, f32 scale);

private:
    struct Line {
        u32 offset = 0, length = 0; // bytes of shown_
        f32 width = 0;              // pixels
    };
    // Holds one item inline, so a new one-font, one-line label (a floating "+10") doesn't allocate.
    template <typename T>
    class SmallList {
    public:
        size_t size() const { return n_; }
        bool empty() const { return n_ == 0; }
        void clear() { n_ = 0; }
        void resize(size_t n) {
            if (n > 1 && heap_.size() < n) heap_.resize(n);
            n_ = n;
        }
        T& operator[](size_t i) { return n_ <= 1 ? one_ : heap_[i]; }
        const T& operator[](size_t i) const { return n_ <= 1 ? one_ : heap_[i]; }
        bool operator==(std::span<const T> other) const {
            if (other.size() != n_) return false;
            for (size_t i = 0; i < n_; ++i) {
                if (!((*this)[i] == other[i])) return false;
            }
            return true;
        }
        void assign(std::span<const T> items) {
            resize(items.size());
            for (size_t i = 0; i < items.size(); ++i) (*this)[i] = items[i];
        }

    private:
        T one_{};
        std::vector<T> heap_;
        size_t n_ = 0;
    };

    struct RichRun {
        u32 begin = 0, end = 0; // bytes of richText_
        bool icon = false;
        SpriteRegion region;
        f32 iconScale = 1;
        std::optional<Color> color;
        u32 fontBase = 0;       // richFonts_[fontBase + shrink step]
    };
    struct Piece {
        u32 run = 0;
        u32 begin = 0, end = 0;  // visible bytes (trailing spaces and newline excluded)
        f32 width = 0, advance = 0, ascent = 0, descent = 0, x = 0; // pixels; x in reading order
        f32 vx = 0;               // x as drawn (right-to-left lines reorder pieces)
        i8 dir = 0;               // 1 left-to-right text (Latin, CJK, digits), -1 right-to-left, 0 neutral
        u32 line = 0;
        TTF_Font* font = nullptr; // the one font of the run's chain this piece draws with
        bool breakBefore = false, newline = false, icon = false;
    };
    struct RichLine {
        f32 width = 0, ascent = 0, descent = 0, y = 0; // pixels
        u32 first = 0, count = 0;
    };

    bool layout();
    void resolveRich(const RichResolver& resolver);
    void plainRun();
    bool needsPieces() const;
    bool shapeRich(size_t fontIndex);
    bool measurePiece(size_t i, size_t fontIndex);
    void placeRichLines(f32 baseAscent, f32 baseDescent, f32 baseHeight, f32 baseSkip);
    void stackRichLines(f32 baseHeight, f32 baseSkip);
    void orderLine(const RichLine& line);
    void applyRichEllipsis(size_t fontIndex);
    void drawRich(Vec2 origin, f32 boxPx, f32 k, Color color, f32 scale);
    void releaseRich();
    bool shape(size_t fontIndex);   // lays out shown_ with one font; fills lines_
    bool fits() const;
    void applyKinsoku();
    void applyEllipsis();
    bool refresh();                 // re-lays out after a pixel scale or font change
    void buildLineTexts();

    TextBackend* backend_;
    TtfTextPtr paragraph_;          // the whole paragraph: wrapping and measuring
    std::vector<TtfTextPtr> lineTexts_; // one per line, drawn when there is more than one line
    SmallList<Line> lines_;
    SmallList<FontHandle> fonts_;
    std::string text_, shown_;
    f32 wrapWidth_ = 0, lineSpacing_ = 1;
    i32 maxLines_ = 0;
    Vec2 fitBox_{};
    bool rtl_ = false;
    size_t fontIndex_ = 0;
    Vec2 sizePx_{};
    f32 lineStepPx_ = 0;
    u32 fontGeneration_ = 0;
    Color color_{0, 0, 0, 0};
    bool linesDirty_ = true;        // lineTexts_ must be rebuilt from lines_
    bool laidOut_ = false;

    // Rich text.
    bool rich_ = false;
    bool hadResolver_ = false;
    u32 richRevision_ = 0;
    std::string richText_;          // visible text (then "…" when truncated)
    u32 richTextLen_ = 0;           // richText_ length before any ellipsis
    std::vector<MarkupRun> markup_;
    std::vector<RichRun> runs_;
    std::vector<FontHandle> richFonts_;
    std::vector<u32> breaks_;
    std::vector<Piece> pieces_;
    std::vector<TtfTextPtr> pieceTexts_; // pieceTexts_[i] draws pieces_[i] (none for icons)
    std::vector<RichLine> richLines_;
    std::vector<PlacedIcon> placed_;
};

} // namespace nova
