#include <audio/audio_backend.hpp>
#include <doctest/doctest.h>
#include <nova/assets/assets.hpp>

#include <SDL3/SDL.h>

using namespace nova;

namespace {

struct AudioFixture {
    AudioFixture() { REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO)); }
    ~AudioFixture() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
};

Assets makeTexturelessAssets() {
    return Assets([](std::string_view) -> std::optional<TextureHandle> { return std::nullopt; },
                  [](TextureHandle) {}, TextureHandle{});
}

void wireAudio(Assets& assets, AudioBackend& audio) {
    assets.setAudioLoaders(
        [&audio](std::string_view p) { return audio.loadSound(p); }, [&audio](SoundHandle h) { audio.unloadSound(h); },
        [&audio](std::string_view p) { return audio.loadMusic(p); }, [&audio](MusicHandle h) { audio.unloadMusic(h); });
}

} // namespace

TEST_CASE("Audio loads generated wav and plays with voice stealing") {
    AudioFixture fx;
    auto audio = AudioBackend::create();
    REQUIRE(audio);
    REQUIRE(audio->available());
    auto hit = audio->loadSound("sfx/hit.wav");
    REQUIRE(hit.has_value());
    for (int i = 0; i < 20; ++i) audio->play(*hit, {.volume = 0.5f, .pitch = 1.2f}); // > 16 tracks
    auto music = audio->loadMusic("music/loop.wav");
    REQUIRE(music.has_value());
    audio->playMusic(*music, {.loop = true, .fadeInMs = 100});
    audio->pauseAll();
    audio->resumeAll();
    audio->stopMusic(50);
    audio->unloadSound(*hit);
    audio->unloadMusic(*music);
    CHECK(audio->liveAudioCount() == 0);
}

TEST_CASE("Audio missing file yields invalid handle and silent play") {
    AudioFixture fx;
    auto audio = AudioBackend::create();
    Assets assets = makeTexturelessAssets();
    wireAudio(assets, *audio);
    assets.setActiveScope(1);
    SoundHandle missing = assets.sound("sfx/nope.wav");
    CHECK(!missing.valid());
    audio->play(missing); // no crash
    audio->playMusic(assets.music("music/nope.wav"));
    SoundHandle hit = assets.sound("sfx/hit.wav");
    CHECK(hit.valid());
    CHECK(assets.sound("sfx/hit.wav") == hit); // cached
    assets.releaseScope(1);
    assets.purgeUnreferenced();
    CHECK(audio->liveAudioCount() == 0);
}

TEST_CASE("Audio volume clamps and an unavailable device is a silent no-op") {
    AudioFixture fx;
    auto audio = AudioBackend::create();
    audio->setVolume(Bus::Sfx, 3.f);
    CHECK(audio->volume(Bus::Sfx) == 1.f);
    audio->setVolume(Bus::Music, -1.f);
    CHECK(audio->volume(Bus::Music) == 0.f);

    auto offline = AudioBackend::createUnavailable();
    CHECK(!offline->available());
    CHECK(!offline->loadSound("sfx/hit.wav").has_value());
    offline->play(SoundHandle{0, 1});
    offline->setVolume(Bus::Master, 0.5f);
    CHECK(offline->volume(Bus::Master) == 0.5f);
}
