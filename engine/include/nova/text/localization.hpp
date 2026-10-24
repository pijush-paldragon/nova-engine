#pragma once

// Translated strings: assets/text/<lang>.json files, looked up by dotted key ("menu.play"), with
// {placeholders} and plural forms. Reached with Scene::text().lang().

#include <nova/core/types.hpp>

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
