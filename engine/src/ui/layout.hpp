#pragma once

#include "ui/element_store.hpp"

namespace nova::ui {

// Content size of a leaf for `fit` axes (label text, sprite size); null means {0, 0}.
using MeasureFn = Vec2 (*)(const Element& element, void* ctx);

// Lays out every dirty layer (all of them when env changed since the last call). Returns the
// number of elements laid out: 0 when nothing changed. Iterative and allocation-free.
u32 layoutCanvas(ElementStore& store, const LayoutEnv& env, MeasureFn measure, void* ctx);

// An element and everything that can change its size: marks its layer for layout.
void markLayoutDirty(ElementStore& store, UiHandle h);

// The box as drawn and hit-tested: the layout box under its own and its ancestors' offsets and
// scales (element scale only; a style's pressed scale is visual and never shrinks the hit area).
Rect visualBox(const ElementStore& store, UiHandle h);

} // namespace nova::ui
