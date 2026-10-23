#pragma once

namespace nova {

class RendererBackend;
class InputBackend;
class Assets;
class AudioBackend;
class SaveStore;
class Random;
class FrameArena;
struct AppOptions;

// Engine subsystems a scene can reach. Owned by the App; tests may leave unused ones null.
struct Services {
    RendererBackend* renderer = nullptr;
    InputBackend* input = nullptr;
    Assets* assets = nullptr;
    AudioBackend* audio = nullptr;
    SaveStore* save = nullptr;
    const AppOptions* options = nullptr;
    Random* random = nullptr;
    FrameArena* frame = nullptr;
};

} // namespace nova
