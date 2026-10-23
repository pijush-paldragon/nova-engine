#pragma once

#include <nova/audio/audio.hpp>

#include <optional>
#include <string_view>

namespace nova {

// Engine-side audio: owns the SDL_mixer device, tracks and every loaded sound/music.
class AudioBackend final : public Audio {
public:
    // Opens the default playback device; falls back to an unavailable (silent) instance.
    static std::unique_ptr<AudioBackend> create();
    static std::unique_ptr<AudioBackend> createUnavailable();
    ~AudioBackend();

    std::optional<SoundHandle> loadSound(std::string_view path);
    std::optional<MusicHandle> loadMusic(std::string_view path);
    void unloadSound(SoundHandle sound);
    void unloadMusic(MusicHandle music);
    u32 liveAudioCount() const;

private:
    AudioBackend() = default;
};

} // namespace nova
