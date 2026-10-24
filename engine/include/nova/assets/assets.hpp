#pragma once

#include <nova/assets/asset_cache.hpp>
#include <nova/assets/atlas.hpp>
#include <nova/audio/audio.hpp>
#include <nova/render/types.hpp>
#include <nova/text/text.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace nova {

class Assets;

// Sentinel scope: "the scene currently running".
inline constexpr ScopeId kActiveScope = UINT32_MAX;

// Named regions of one texture, loaded from "<name>.atlas".
class Atlas {
public:
    Atlas() = default;
    bool valid() const;
    // A missing region (or invalid atlas) returns the checkerboard texture and logs once.
    SpriteRegion region(std::string_view name) const;

private:
    friend class Assets;
    struct Tag;
    Atlas(Assets* owner, Handle<Tag> handle) : owner_(owner), handle_(handle) {}

    Assets* owner_ = nullptr;
    Handle<Tag> handle_;
};

// Game-facing asset loader. Loads are cached by path and tied to a scope: the running
// scene by default, or kGlobalScope for assets that live for the whole game. A scene's
// assets are released when it exits (unless another live scope still uses them).
// Missing files never crash: textures fall back to a checkerboard, and the error is logged.
class Assets {
public:
    using TextureLoader = std::function<std::optional<TextureHandle>(std::string_view path)>;
    using TextureUnloader = std::function<void(TextureHandle)>;

    Assets(TextureLoader loadTexture, TextureUnloader unloadTexture, TextureHandle fallback);
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    TextureHandle texture(std::string_view path, ScopeId scope = kActiveScope);
    Atlas atlas(std::string_view name, ScopeId scope = kActiveScope);
    // Invalid handle (silent playback) if the file is missing or audio is unavailable.
    SoundHandle sound(std::string_view path, ScopeId scope = kActiveScope);
    MusicHandle music(std::string_view path, ScopeId scope = kActiveScope);
    // sizePx is in design units; the engine rasterises at the screen's real pixel size.
    FontHandle font(std::string_view path, f32 sizePx, ScopeId scope = kActiveScope);
    // A synthetic bold and/or italic rendering of the font (cached separately from the plain one).
    FontHandle font(std::string_view path, f32 sizePx, FontStyle style, ScopeId scope = kActiveScope);

    // Engine: audio loaders are injected once the audio subsystem exists.
    using SoundLoader = std::function<std::optional<SoundHandle>(std::string_view)>;
    using SoundUnloader = std::function<void(SoundHandle)>;
    using MusicLoader = std::function<std::optional<MusicHandle>(std::string_view)>;
    using MusicUnloader = std::function<void(MusicHandle)>;
    void setAudioLoaders(SoundLoader loadSound, SoundUnloader unloadSound, MusicLoader loadMusic,
                         MusicUnloader unloadMusic);
    using FontLoader = std::function<std::optional<FontHandle>(std::string_view key)>;
    using FontUnloader = std::function<void(FontHandle)>;
    void setFontLoader(FontLoader loadFont, FontUnloader unloadFont);
    // Engine: fonts are loaded per language (each language adds its fallback fonts).
    void setLocalization(const Localization* lang) { lang_ = lang; }

    // Engine: scope bookkeeping driven by SceneStack.
    void setActiveScope(ScopeId scope) { activeScope_ = scope; }
    ScopeId activeScope() const { return activeScope_; }
    void releaseScope(ScopeId scope);
    void purgeUnreferenced();
    u32 loadedCount() const;
    TextureHandle fallbackTexture() const { return fallback_; }

private:
    friend class Atlas;
    struct AtlasData {
        TextureHandle texture;
        std::string imagePath;
        std::vector<AtlasEntry> regions;
    };

    ScopeId resolve(ScopeId scope) const { return scope == kActiveScope ? activeScope_ : scope; }
    SpriteRegion regionOf(Handle<Atlas::Tag> atlas, std::string_view name);
    std::optional<AtlasData> loadAtlas(std::string_view name);

    TextureHandle fallback_;
    ScopeId activeScope_ = kGlobalScope;
    AssetCache<TextureHandle, TextureTag> textures_;
    AssetCache<AtlasData, Atlas::Tag> atlases_;
    std::optional<AssetCache<SoundHandle, SoundTag>> sounds_;
    std::optional<AssetCache<MusicHandle, MusicTag>> musics_;
    std::optional<AssetCache<FontHandle, FontTag>> fonts_;
    const Localization* lang_ = nullptr;
    std::unordered_set<std::string> reportedMissing_;
};

} // namespace nova
