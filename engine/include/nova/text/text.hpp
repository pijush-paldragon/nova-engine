#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/math.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace nova {

struct FontTag;
using FontHandle = Handle<FontTag>;

enum class TextAlign : u8 { Left, Center, Right };

// A line of text drawn at its entity's Transform: horizontally anchored per `align`,
// vertically centred. Text always draws above the scene's sprites; `layer` orders labels.
// Changing the string re-lays out the text only when the content actually differs.
struct TextLabel {
    FontHandle font;
    Color color = Color::White;
    TextAlign align = TextAlign::Left;
    u8 layer = 0;
    bool visible = true;

    void set(std::string_view s) {
        if (s == text_) return;
        text_.assign(s.data(), s.size()); // reuses capacity: no allocation for same-length updates
        ++revision_;
    }
    const std::string& string() const { return text_; }
    u32 revision() const { return revision_; }

    // Managed through set(); public only so TextLabel stays an aggregate (designated initializers).
    std::string text_;
    u32 revision_ = 0;
};

// Game-facing text services.
class Text {
public:
    // Size of the string in design units.
    Vec2 measure(FontHandle font, std::string_view s) const;
    u32 rebuildsTotal() const { return rebuilds_; }

protected:
    struct Impl;
    Text();
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    std::unique_ptr<Impl> impl_;
    u32 rebuilds_ = 0;
    f32 pixelScale_ = 1;
};

} // namespace nova
