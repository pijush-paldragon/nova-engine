#pragma once

#include "ui/style.hpp"

#include <nova/ui/types.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nova::ui {

class Canvas;
struct UiShared;

// Reads a file's text by asset-root-relative path; nullopt when it doesn't exist.
using FileReader = std::function<std::optional<std::string>(std::string_view path)>;

// Reads through openAsset (the asset pack in release builds, loose files otherwise).
FileReader assetFileReader();

// One element of a parsed layout, in pre-order; parent indexes into the same vector (-1: root).
struct NodeSpec {
    UiSpec spec;
    Kind kind = Kind::Panel;
    i32 parent = -1;
};

struct LoadResult {
    bool ok = false;                 // false: nothing usable (missing file, syntax error, include cycle)
    std::string firstError;          // the first fatal error or validation message
    std::vector<std::string> errors; // every validation message, in file order
    std::vector<std::string> files;  // every file read: the layout, then its includes
    Transition enter = Transition::None;
    Transition exit = Transition::None;
};

// Parses a layout file. Validation problems (unknown keys, wrong types, unknown styles when a
// theme is given, unknown types, duplicate ids) are reported in `errors` and fall back to
// defaults; only unreadable input fails the load.
LoadResult loadLayout(std::string_view path, const FileReader& read, const Theme* theme, std::vector<NodeSpec>& out);

// Parses `{"styles": {...}}` and sets each style into `theme`.
LoadResult loadTheme(std::string_view path, const FileReader& read, Theme& theme);

// The game's ui/theme.json, if it exists, into the shared theme (errors logged in dev builds).
void loadGameTheme(UiShared& shared, const FileReader& read);

// Engine hook: where Canvas::load reads files (tests and live reload swap it).
void uiSetFileReader(Canvas& canvas, FileReader read);

} // namespace nova::ui
