#include <nova/assets/assets.hpp>
#include <nova/core/log.hpp>
#include <nova/platform/file.hpp>

namespace nova {

bool Atlas::valid() const { return owner_ && owner_->atlases_.get(handle_) != nullptr; }

SpriteRegion Atlas::region(std::string_view name) const {
    if (!owner_) return {};
    return owner_->regionOf(handle_, name);
}

Assets::Assets(TextureLoader loadTexture, TextureUnloader unloadTexture, TextureHandle fallback)
    : fallback_(fallback),
      textures_(std::move(loadTexture), [unloadTexture](TextureHandle& h) { unloadTexture(h); }),
      atlases_([this](std::string_view name) { return loadAtlas(name); }, [](AtlasData&) {}) {}

TextureHandle Assets::texture(std::string_view path, ScopeId scope) {
    auto handle = textures_.acquire(path, resolve(scope));
    TextureHandle* texture = textures_.get(handle);
    return texture ? *texture : fallback_;
}

void Assets::setAudioLoaders(SoundLoader loadSound, SoundUnloader unloadSound, MusicLoader loadMusic,
                             MusicUnloader unloadMusic) {
    sounds_.emplace(std::move(loadSound), [unloadSound](SoundHandle& h) { unloadSound(h); });
    musics_.emplace(std::move(loadMusic), [unloadMusic](MusicHandle& h) { unloadMusic(h); });
}

SoundHandle Assets::sound(std::string_view path, ScopeId scope) {
    if (!sounds_) return {};
    SoundHandle* h = sounds_->get(sounds_->acquire(path, resolve(scope)));
    return h ? *h : SoundHandle{};
}

MusicHandle Assets::music(std::string_view path, ScopeId scope) {
    if (!musics_) return {};
    MusicHandle* h = musics_->get(musics_->acquire(path, resolve(scope)));
    return h ? *h : MusicHandle{};
}

Atlas Assets::atlas(std::string_view name, ScopeId scope) {
    ScopeId s = resolve(scope);
    auto handle = atlases_.acquire(name, s);
    if (AtlasData* data = atlases_.get(handle)) {
        // Tag the atlas image with the same scope, so the texture lives exactly as long as the atlas.
        auto texHandle = textures_.acquire(data->imagePath, s);
        TextureHandle* tex = textures_.get(texHandle);
        data->texture = tex ? *tex : fallback_;
    }
    return Atlas(this, handle);
}

std::optional<Assets::AtlasData> Assets::loadAtlas(std::string_view name) {
    std::string file = std::string(name) + ".atlas";
    auto text = readTextFile(file);
    if (!text) {
        log::error("Atlas '%s' not found: %s", file.c_str(), text.error().message.c_str());
        return std::nullopt;
    }
    auto image = parseAtlasImage(text.value());
    auto regions = parseAtlas(text.value());
    if (!image || !regions) {
        log::error("Atlas '%s' is malformed: %s", file.c_str(),
                   (!image ? image.error() : regions.error()).message.c_str());
        return std::nullopt;
    }
    // The image path is relative to the atlas file's folder.
    std::string dir;
    if (size_t slash = file.find_last_of('/'); slash != std::string::npos) dir = file.substr(0, slash + 1);
    return AtlasData{fallback_, dir + image.value(), std::move(regions.value())};
}

SpriteRegion Assets::regionOf(Handle<Atlas::Tag> atlas, std::string_view name) {
    if (AtlasData* data = atlases_.get(atlas)) {
        for (const AtlasEntry& e : data->regions) {
            if (e.name == name) return {data->texture, e.src};
        }
    }
    std::string key(name);
    if (reportedMissing_.insert(key).second) log::error("Atlas region '%s' not found", key.c_str());
    return {fallback_, {}};
}

void Assets::releaseScope(ScopeId scope) {
    atlases_.releaseScope(scope);
    textures_.releaseScope(scope);
    if (sounds_) sounds_->releaseScope(scope);
    if (musics_) musics_->releaseScope(scope);
}

void Assets::purgeUnreferenced() {
    atlases_.purgeUnreferenced();
    textures_.purgeUnreferenced();
    if (sounds_) sounds_->purgeUnreferenced();
    if (musics_) musics_->purgeUnreferenced();
}

u32 Assets::loadedCount() const {
    return textures_.loadedCount() + atlases_.loadedCount() + (sounds_ ? sounds_->loadedCount() : 0) +
           (musics_ ? musics_->loadedCount() : 0);
}

} // namespace nova
