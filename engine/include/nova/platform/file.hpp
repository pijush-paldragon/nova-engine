#pragma once

#include <nova/core/result.hpp>
#include <nova/core/types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace nova {

// Resolves a path relative to the game's assets root (bundle Resources on Apple,
// APK assets on Android, which SDL reads directly from relative paths).
std::string assetPath(std::string_view relative);

// Overrides the assets root (e.g. point a desktop build or tests at source assets).
// The root must end with a path separator. An empty root restores the platform default.
void setAssetRoot(std::string_view root);

Result<std::vector<u8>> readFile(std::string_view relative);
Result<std::string> readTextFile(std::string_view relative);

} // namespace nova
