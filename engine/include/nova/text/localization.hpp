#pragma once

// Translated strings: assets/text/<lang>.json files, looked up by dotted key ("menu.play"), with
// {placeholders} and plural forms. Reached with Scene::text().lang().

#include <nova/core/types.hpp>

#include <array>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace nova {

// One {name} placeholder value.
struct FormatArg {
    std::string_view name;
    std::variant<i64, f64, std::string_view> value;
};

// Up to 4 placeholder values kept inline (names up to 15 bytes, text values up to 31 bytes), so a
// label can keep its arguments without allocating. Longer values are cut.
struct KeyArgs {
    static constexpr size_t kMax = 4;
    struct Stored {
        char name[16]{};
        u8 kind = 0; // 0 integer, 1 real, 2 text
        i64 i = 0;
        f64 f = 0;
        char s[32]{};
    };
    std::array<Stored, kMax> items{};
    u8 count = 0;

    // Replaces the values; true if anything changed.
    bool assign(std::initializer_list<FormatArg> args);
    // FormatArgs pointing into this object.
    size_t view(std::array<FormatArg, kMax>& out) const;
};

class Localization {
public:
    Localization();
    ~Localization();
    Localization(const Localization&) = delete;
    Localization& operator=(const Localization&) = delete;

    // The translation; falls back to English, then to "[key]" (logged once in dev builds).
    std::string_view get(std::string_view key) const;
    // Writes the translation with {placeholders} filled into `out` (always NUL-terminated,
    // truncated to fit) and returns its length. Never allocates.
    size_t format(std::span<char> out, std::string_view key, std::initializer_list<FormatArg> args = {}) const;
    size_t formatArgs(std::span<char> out, std::string_view key, std::span<const FormatArg> args) const;
    // Picks the plural form for `count` (CLDR rules for the language) and formats it; {count} is
    // filled in automatically.
    size_t plural(std::span<char> out, std::string_view key, i64 count, std::initializer_list<FormatArg> args = {}) const;

    // Loads text/<lang>.json; false (and nothing changes) if it doesn't exist.
    bool setLanguage(std::string_view lang);
    std::string_view language() const;
    // Languages listed in text/languages.json (or just "en").
    std::span<const std::string> available() const;
    bool rightToLeft() const;
    // Changes with every language switch or reload: labels re-render when it differs.
    u32 generation() const;

    // Engine use only.
    struct Impl;
    Impl& impl() { return *impl_; }
    const Impl& impl() const { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace nova
