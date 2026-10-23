#include <nova/platform/file.hpp>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

namespace nova {
namespace {

std::string& rootOverride() {
    static std::string root;
    return root;
}

std::string platformRoot() {
#if defined(SDL_PLATFORM_ANDROID)
    return {}; // relative paths are read from the APK's assets
#else
    const char* base = SDL_GetBasePath(); // owned by SDL, cached
    return base ? std::string(base) : std::string();
#endif
}

} // namespace

void setAssetRoot(std::string_view root) { rootOverride() = std::string(root); }

std::string assetPath(std::string_view relative) {
    const std::string& root = rootOverride().empty() ? platformRoot() : rootOverride();
    std::string path = root;
    path += relative;
    return path;
}

Result<std::vector<u8>> readFile(std::string_view relative) {
    std::string path = assetPath(relative);
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return Result<std::vector<u8>>::fail("Cannot read " + path + ": " + SDL_GetError());
    std::vector<u8> bytes(static_cast<u8*>(data), static_cast<u8*>(data) + size);
    SDL_free(data);
    return bytes;
}

Result<std::string> readTextFile(std::string_view relative) {
    std::string path = assetPath(relative);
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return Result<std::string>::fail("Cannot read " + path + ": " + SDL_GetError());
    std::string text(static_cast<const char*>(data), size);
    SDL_free(data);
    return text;
}

} // namespace nova
