#pragma once

#include "ui/canvas_impl.hpp"

namespace nova::ui {

inline constexpr f32 kPressSeconds = 0.08f; // press/release scale ease

// Advances element animations, progress bars and press easing. Never allocates.
void updateAnimations(Canvas::Impl& impl, f32 dt);

// Popup enter/exit: sets the layer's content and backdrop for its current phase and time.
void applyTransition(Canvas::Impl& impl, u16 layer);
// Advances popup transitions; finished exits remove their layer and run onClose.
void updateTransitions(Canvas::Impl& impl, f32 dt);

} // namespace nova::ui
