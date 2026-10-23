#pragma once

#include <nova/core/handle.hpp>
#include <nova/core/math.hpp>

#include <array>
#include <memory>

namespace nova {

struct SoundTag;
using SoundHandle = Handle<SoundTag>;
struct MusicTag;
using MusicHandle = Handle<MusicTag>;

enum class Bus : u8 { Master, Sfx, Music };

struct PlayParams {
    f32 volume = 1;
    f32 pitch = 1; // playback speed ratio
};

struct MusicParams {
    bool loop = true;
    i32 fadeInMs = 0;
};

// Game-facing audio. Sound effects are decoded up front and play instantly on a pool of
// tracks (the oldest is stolen when all are busy); music streams from disk on its own track.
// Every call is a silent no-op for invalid handles or when no audio device is available.
class Audio {
public:
    void play(SoundHandle sound, PlayParams params = {});
    void playMusic(MusicHandle music, MusicParams params = {});
    void stopMusic(i32 fadeOutMs = 0);

    void setVolume(Bus bus, f32 volume); // clamped to [0, 1]
    f32 volume(Bus bus) const { return volumes_[size_t(bus)]; }

    void pauseAll();
    void resumeAll();
    bool available() const;

protected:
    struct Impl;
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    std::unique_ptr<Impl> impl_;
    std::array<f32, 3> volumes_{1, 1, 1};
};

} // namespace nova
