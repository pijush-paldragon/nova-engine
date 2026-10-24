#pragma once

#include <nova/core/math.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nova {

// The look a run of rich text asks for; names are resolved by the caller (theme styles/colours).
struct RunStyle {
    bool bold = false, italic = false;
    f32 sizeScale = 1;
    std::optional<Color> color;  // [color=#rrggbb(aa)]
    std::string_view colorName;  // [color=name]
    std::string_view styleName;  // [style=name]
    bool plain() const { return !bold && !italic && sizeScale == 1 && !color && colorName.empty() && styleName.empty(); }
};

struct MarkupRun {
    enum class Kind : u8 { Text, Icon };
    Kind kind = Kind::Text;
    std::string_view text; // visible text, a slice of the parse's storage
    RunStyle style;
    std::string_view icon; // "atlas/region"
    f32 iconScale = 1;
};

// Parses BBCode-style markup: [b] [i] [color=#rrggbb(aa)|name] [size=s] [style=name] (closed by
// [/b] …) and [icon=atlas/region scale=s]; "[[" is a literal "[". Tags nest. Unknown, malformed,
// unclosed or stray closing tags stay in the text as written, each with an error. `storage`
// receives the visible text the runs point into; names point into `src`, which must outlive the
// runs. Returns false when `src` has no markup at all (no "["): use the plain text path.
bool parseMarkup(std::string_view src, std::string& storage, std::vector<MarkupRun>& out,
                 std::vector<std::string>* errors);

// `value` with each "[" doubled, so substituted text shows literally inside markup. Writes into
// buf (cut to fit) and returns the written part.
std::string_view escapeMarkup(std::string_view value, std::span<char> buf);

} // namespace nova
