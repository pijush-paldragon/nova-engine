#pragma once

namespace nova {

class RendererBackend;
class InputBackend;
class Assets;
class AudioBackend;
class TextBackend;
class SaveStore;
class Random;
class FrameArena;
struct AppOptions;
namespace ui {
struct UiShared;
}

// Engine subsystems a scene can reach. Owned by the App; tests may leave unused ones null.
struct Services {
    RendererBackend* renderer = nullptr;
    InputBackend* input = nullptr;
    Assets* assets = nullptr;
    AudioBackend* audio = nullptr;
    TextBackend* text = nullptr;
    SaveStore* save = nullptr;
    const AppOptions* options = nullptr;
    Random* random = nullptr;
    FrameArena* frame = nullptr;
    ui::UiShared* ui = nullptr; // shared UI theme and textures
};

} // namespace nova
