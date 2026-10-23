#pragma once

#include <nova/core/math.hpp>
#include <nova/core/result.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace nova {

struct AtlasEntry {
    std::string name;
    Rect src;
};

// Atlas text format:
//   # comment
//   image sprites.png
//   <name> <x> <y> <w> <h>
Result<std::vector<AtlasEntry>> parseAtlas(std::string_view text);
Result<std::string> parseAtlasImage(std::string_view text);

} // namespace nova
