#pragma once

#include <nova/core/math.hpp>
#include <nova/render/camera.hpp>
#include <nova/text/text.hpp>
#include <nova/ui/types.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct TTF_Text;

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

struct TextBlockParams {
    // [0] is the nominal size; with a fitBox, further entries are the shrink steps, largest first.
    std::span<const FontHandle> fonts;
    std::string_view text;
    f32 wrapWidth = 0;    // design units; 0 = break only at newlines
    i32 maxLines = 0;     // 0 = no limit; overflow ends with "…"
    f32 lineSpacing = 1;
    Vec2 fitBox{};        // non-zero: pick the largest font whose text fits (0 on an axis = unbounded)
    bool rtl = false;     // paragraph direction, for Start/End alignment
};

// A paragraph of plain text: wrapped, truncated to maxLines, shrunk to fit and aligned per line.
// Re-lays out only when an input, a font or the pixel scale changed; drawing and unchanged
// set() calls never allocate.
class TextBlock {
public:
    explicit TextBlock(TextBackend& backend) : backend_(&backend) {}
    TextBlock(TextBlock&&) noexcept = default;
    TextBlock& operator=(TextBlock&&) noexcept = default;

    // False if the font is gone or the text could not be created.
    bool set(const TextBlockParams& p);
    // Size in design units ({0, 0} for empty text).
    Vec2 size();
    u32 lineCount() const { return u32(lines_.size()); }
    size_t fontIndex() const { return fontIndex_; }
    bool empty() const { return text_.empty(); }
    // The text as laid out (with any inserted line breaks and the ellipsis), and one line of it.
    std::string_view shownText() const { return shown_; }
    std::string_view lineText(u32 line) const;

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

    bool layout();
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
};

} // namespace nova
