#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/math.hpp>
#include <nova/text/localization.hpp>

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

    // Literal text.
    void set(std::string_view s) {
        if (key_.empty() && s == text_) return;
        key_.clear();
        text_.assign(s.data(), s.size()); // reuses capacity: no allocation for same-length updates
        ++revision_;
    }
    // A translation key with {placeholder} values; re-renders when the language changes.
    void setKey(std::string_view key, std::initializer_list<FormatArg> args = {}) {
        const bool sameKey = key == key_;
        if (!sameKey) key_.assign(key.data(), key.size());
        if (args_.assign(args) || !sameKey) ++revision_;
    }
    const std::string& string() const { return text_; }
    // What is drawn: the literal text, or the translation of the key.
    const std::string& displayed() const { return key_.empty() ? text_ : shown_; }
    u32 revision() const { return revision_; }

    // Managed through set()/setKey(); public only so TextLabel stays an aggregate (designated initializers).
    std::string text_;
    u32 revision_ = 0;
    std::string key_;
    KeyArgs args_;
    std::string shown_; // the rendered translation
    u32 langGen_ = 0;
};

// Game-facing text services.
class Text {
public:
    // Size of the string in design units.
    Vec2 measure(FontHandle font, std::string_view s) const;
    u32 rebuildsTotal() const { return rebuilds_; }
    // Translated strings for the current language.
    Localization& lang() { return *lang_; }

protected:
    struct Impl;
    Text();
    ~Text();
    Text(const Text&) = delete;
    Text& operator=(const Text&) = delete;

    std::unique_ptr<Impl> impl_;
    std::unique_ptr<Localization> lang_;
    u32 rebuilds_ = 0;
    f32 pixelScale_ = 1;
};

} // namespace nova
