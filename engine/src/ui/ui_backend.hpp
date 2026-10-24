#pragma once

// Engine-side UI hooks, called by the App and SceneStack (game code never calls these).

#include <nova/core/types.hpp>

namespace nova {
struct Services;
}

namespace nova::ui {

class Canvas;

void uiAttach(Canvas& canvas, Services* services);
// Frees elements removed this frame.
void uiFlush(Canvas& canvas);
// Advances the UI by dt and re-runs layout where something changed.
void uiUpdate(Canvas& canvas, f32 dt);

} // namespace nova::ui
