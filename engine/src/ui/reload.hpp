#pragma once

// Live reload of layout files and the theme, for desktop dev builds: the game's entry point
// passes its source assets folder, and canvases poll the files they were built from.

#include "ui/canvas_impl.hpp"

namespace nova::detail {
void setDevAssetSourceDir(const char* dir); // "" turns reload off
const char* devAssetSourceDir();
} // namespace nova::detail

namespace nova::ui {

inline constexpr f32 kReloadInterval = 0.5f; // seconds between checks

// Reads `<source dir>/<path>`.
FileReader sourceFileReader();

// Remembers the modification times of the files a layer (or the theme) was built from.
void recordLayerStamps(Canvas::Impl& impl, u16 layer);
void recordThemeStamps(UiShared& shared, const std::vector<std::string>& files);

// Every kReloadInterval: reloads changed layers and the changed theme. Dev builds only.
void pollReload(Canvas::Impl& impl, f32 dt);

// Puts remembered values (set through references, or by the player) back on a rebuilt layer.
void reapplyRemembered(Canvas::Impl& impl, u16 layer);

} // namespace nova::ui
