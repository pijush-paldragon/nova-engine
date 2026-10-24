#pragma once

// Engine-side UI hooks, called by the App and SceneStack (game code never calls these).

namespace nova {
struct Services;
}

namespace nova::ui {

class Canvas;

void uiAttach(Canvas& canvas, Services* services);
// Frees elements removed this frame.
void uiFlush(Canvas& canvas);

} // namespace nova::ui
