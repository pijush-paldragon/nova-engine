#pragma once

#include "ui/canvas_impl.hpp"

namespace nova::ui {

inline constexpr f32 kCancelDistance = 16; // design px a finger may stray outside before a press cancels

bool isInteractive(Kind kind);

// The topmost enabled interactive element under `pos` (searching layers top-down), or invalid.
// `blocked` is set when a non-interactive element with blocksInput stops the search first.
UiHandle hitTest(Canvas::Impl& impl, Vec2 pos, bool& blocked);

} // namespace nova::ui
