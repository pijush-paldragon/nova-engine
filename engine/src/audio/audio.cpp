#include "audio/audio_backend.hpp"

#include <nova/core/log.hpp>
#include <nova/platform/file.hpp>

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <array>

namespace nova {
namespace {

constexpr size_t kSfxTracks = 16;

struct AudioDeleter {
    void operator()(MIX_Audio* a) const { MIX_DestroyAudio(a); }
};
using AudioPtr = std::unique_ptr<MIX_Audio, AudioDeleter>;

} // namespace

struct Audio::Impl {
    MIX_Mixer* mixer = nullptr;
    std::array<MIX_Track*, kSfxTracks> sfx{};
    std::array<u64, kSfxTracks> startedAt{}; // play counter, for stealing the oldest voice
    MIX_Track* music = nullptr;
    u64 plays = 0;
    HandlePool<AudioPtr, SoundTag> sounds;
    HandlePool<AudioPtr, MusicTag> musics;

    ~Impl() {
        for (MIX_Track* t : sfx) {
            if (t) MIX_DestroyTrack(t);
        }
        if (music) MIX_DestroyTrack(music);
        sounds.clear(); // audio data after the tracks that play it
        musics.clear();
        if (mixer) {
            MIX_DestroyMixer(mixer);
            MIX_Quit();
        }
    }
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() = default;

bool Audio::available() const { return impl_->mixer != nullptr; }

void Audio::play(SoundHandle sound, PlayParams params) {
    AudioPtr* data = impl_->sounds.get(sound);
    if (!available() || !data) return;

    size_t pick = 0;
    for (size_t i = 0; i < kSfxTracks; ++i) {
        if (!MIX_TrackPlaying(impl_->sfx[i])) {
            pick = i;
            break;
        }
        if (impl_->startedAt[i] < impl_->startedAt[pick]) pick = i; // all busy: steal the oldest
    }
    MIX_Track* track = impl_->sfx[pick];
    impl_->startedAt[pick] = ++impl_->plays;
    MIX_SetTrackAudio(track, data->get());
    MIX_SetTrackGain(track, clamp(params.volume, 0.f, 1.f) * volumes_[size_t(Bus::Sfx)]);
    MIX_SetTrackFrequencyRatio(track, clamp(params.pitch, 0.1f, 10.f));
    MIX_PlayTrack(track, 0);
}

void Audio::playMusic(MusicHandle music, MusicParams params) {
    AudioPtr* data = impl_->musics.get(music);
    if (!available() || !data) return;
    MIX_SetTrackAudio(impl_->music, data->get());
    MIX_SetTrackGain(impl_->music, volumes_[size_t(Bus::Music)]);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, params.loop ? -1 : 0);
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_FADE_IN_MILLISECONDS_NUMBER, params.fadeInMs);
    MIX_PlayTrack(impl_->music, props);
    SDL_DestroyProperties(props);
}

void Audio::stopMusic(i32 fadeOutMs) {
    if (!available()) return;
    MIX_StopTrack(impl_->music, MIX_TrackMSToFrames(impl_->music, fadeOutMs));
}

void Audio::setVolume(Bus bus, f32 volume) {
    volume = clamp(volume, 0.f, 1.f);
    volumes_[size_t(bus)] = volume;
    if (!available()) return;
    if (bus == Bus::Master) MIX_SetMixerGain(impl_->mixer, volume);
    if (bus == Bus::Music) MIX_SetTrackGain(impl_->music, volume);
    // Sfx volume applies to the next play() of each effect.
}

void Audio::pauseAll() {
    if (available()) MIX_PauseAllTracks(impl_->mixer);
}

void Audio::resumeAll() {
    if (available()) MIX_ResumeAllTracks(impl_->mixer);
}

// ---- AudioBackend -----------------------------------------------------------------------

std::unique_ptr<AudioBackend> AudioBackend::createUnavailable() {
    return std::unique_ptr<AudioBackend>(new AudioBackend());
}

std::unique_ptr<AudioBackend> AudioBackend::create() {
    auto audio = createUnavailable();
    if (!SDL_WasInit(SDL_INIT_AUDIO) || !MIX_Init()) {
        log::warn("Audio disabled: %s", SDL_GetError());
        return audio;
    }
    MIX_Mixer* mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!mixer) {
        log::warn("Audio disabled, no playback device: %s", SDL_GetError());
        MIX_Quit();
        return audio;
    }
    Impl& impl = *audio->impl_;
    impl.mixer = mixer;
    for (MIX_Track*& t : impl.sfx) t = MIX_CreateTrack(mixer);
    impl.music = MIX_CreateTrack(mixer);
    return audio;
}

AudioBackend::~AudioBackend() = default;

std::optional<SoundHandle> AudioBackend::loadSound(std::string_view path) {
    if (!available()) return std::nullopt;
    std::string full = assetPath(path);
    MIX_Audio* audio = MIX_LoadAudio(impl_->mixer, full.c_str(), true);
    if (!audio) {
        log::error("Sound '%s' failed to load: %s", full.c_str(), SDL_GetError());
        return std::nullopt;
    }
    return impl_->sounds.insert(AudioPtr(audio));
}

std::optional<MusicHandle> AudioBackend::loadMusic(std::string_view path) {
    if (!available()) return std::nullopt;
    std::string full = assetPath(path);
    MIX_Audio* audio = MIX_LoadAudio(impl_->mixer, full.c_str(), false); // streamed
    if (!audio) {
        log::error("Music '%s' failed to load: %s", full.c_str(), SDL_GetError());
        return std::nullopt;
    }
    return impl_->musics.insert(AudioPtr(audio));
}

void AudioBackend::unloadSound(SoundHandle sound) {
    AudioPtr* data = impl_->sounds.get(sound);
    if (!data) return;
    // Detach from any track still playing it before freeing the data.
    for (MIX_Track* t : impl_->sfx) {
        if (t && MIX_GetTrackAudio(t) == data->get()) {
            MIX_StopTrack(t, 0);
            MIX_SetTrackAudio(t, nullptr);
        }
    }
    impl_->sounds.remove(sound);
}

void AudioBackend::unloadMusic(MusicHandle music) {
    AudioPtr* data = impl_->musics.get(music);
    if (!data) return;
    if (impl_->music && MIX_GetTrackAudio(impl_->music) == data->get()) {
        MIX_StopTrack(impl_->music, 0);
        MIX_SetTrackAudio(impl_->music, nullptr);
    }
    impl_->musics.remove(music);
}

u32 AudioBackend::liveAudioCount() const { return impl_->sounds.size() + impl_->musics.size(); }

} // namespace nova
