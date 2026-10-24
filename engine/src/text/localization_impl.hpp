#pragma once

#include <nova/text/localization.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nova {

using TextFileReader = std::function<std::optional<std::string>(std::string_view path)>;

// What the active language needs from fonts and layout (Task 3 and later).
struct LanguageInfo {
    std::string tag = "en";
    bool rtl = false;
    std::vector<std::string> fonts, boldFonts; // fallback fonts, in order
};

struct LangHash {
    using is_transparent = void;
    size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
};

struct Localization::Impl {
    struct Entry {
        std::string text;
        std::array<std::string, 6> forms; // by PluralCategory
        u8 formMask = 0;                  // which forms exist
        bool plural = false;
    };
    using Table = std::unordered_map<std::string, Entry, LangHash, std::equal_to<>>;

    TextFileReader reader;
    Table current, english;
    LanguageInfo info_;
    std::vector<std::string> available;
    u32 generation = 1;
    std::function<void(std::string_view)> onChange; // e.g. save the choice (set by the App)

    void setReader(TextFileReader r);
    const LanguageInfo& info() const { return info_; }
    const Entry* find(std::string_view key) const;
    // Parses text/<lang>.json into `out`; false if the file is missing or unreadable.
    bool loadFile(std::string_view lang, Table& out, LanguageInfo* info);
};

// Reads through openAsset (the asset pack in release builds).
TextFileReader assetTextReader();

} // namespace nova
