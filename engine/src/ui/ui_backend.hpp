#pragma once

// Engine-side UI hooks, called by the App and SceneStack (game code never calls these).

#include <nova/assets/asset_cache.hpp>
#include <nova/core/types.hpp>

namespace nova {
struct Services;
class InputBackend;
class RendererBackend;
class SpriteBatch;
}

namespace nova::ui {

class Canvas;

void uiAttach(Canvas& canvas, Services* services, ScopeId scope);
// Top scene only, before its update: hit-tests touches, tracks ownership, queues events and
// marks the pointers the UI took as consumed.
void uiInput(Canvas& canvas, InputBackend& input);
// Runs the callbacks for this frame's queued events.
void uiDispatch(Canvas& canvas);
// Draws the canvas's layers above its scene: each layer's sprites, then its labels.
void uiDraw(Canvas& canvas, RendererBackend& renderer, SpriteBatch& batch);
// Drops every element and cached resource (text runs) while the services still exist.
void uiReset(Canvas& canvas);
// Frees elements removed this frame.
void uiFlush(Canvas& canvas);
// Advances the UI by dt and re-runs layout where something changed.
void uiUpdate(Canvas& canvas, f32 dt);

} // namespace nova::ui
