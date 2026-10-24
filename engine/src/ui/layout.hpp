#pragma once

#include "ui/element_store.hpp"

namespace nova::ui {

// Content size of a leaf for `fit` axes (label text, sprite size); null means {0, 0}. Text that
// wraps or shrinks is measured for the element's `textBox`.
using MeasureFn = Vec2 (*)(Element& element, void* ctx);

// Lays out every dirty layer (all of them when env changed since the last call). Returns the
// number of elements laid out: 0 when nothing changed. Iterative and allocation-free. A layer
// whose wrapping or shrinking text got a new box is measured and arranged again (up to twice),
// so a wrapped label's fit height follows its arranged width.
u32 layoutCanvas(ElementStore& store, const LayoutEnv& env, MeasureFn measure, void* ctx);

// Pre-sizes layout's scratch space (so a later pass doesn't allocate).
void reserveLayoutScratch(size_t n);

// An element and everything that can change its size: marks its layer for layout.
void markLayoutDirty(ElementStore& store, UiHandle h);

// The box as drawn and hit-tested: the layout box under its own and its ancestors' offsets and
// scales (element scale only; a style's pressed scale is visual and never shrinks the hit area).
Rect visualBox(const ElementStore& store, UiHandle h);

} // namespace nova::ui
