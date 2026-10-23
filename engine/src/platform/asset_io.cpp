#include "asset_io.hpp"

#include "pack_reader.hpp"

#include <nova/core/log.hpp>
#include <nova/platform/file.hpp>

#include <cstdlib>
#include <memory>
#include <string>

namespace nova {

// The test hooks are always compiled; only the engine tests declare them (NOVA_TESTING).
void setIntegrityFailureHandlerForTests(void (*handler)(const char* reason));
const pack::Entry* packEntryForTests(std::string_view path);

namespace {

std::unique_ptr<PackReader>& mounted() {
    static std::unique_ptr<PackReader> reader;
    return reader;
}

void (*&testHandler())(const char*) {
    static void (*handler)(const char*) = nullptr;
    return handler;
}

bool& packRequiredFlag() {
    static bool required = false;
    return required;
}

} // namespace

void detail::setPackRequired(bool required) { packRequiredFlag() = required; }
bool detail::packRequired() { return packRequiredFlag(); }

void reportIntegrityFailure(const char* reason) {
#if defined(NOVA_DEV_TOOLS)
    log::error("Asset pack integrity failure: %s", reason);
#endif
    if (testHandler()) {
        testHandler()(reason);
        return;
    }
    std::_Exit(1);
}

MountResult mountPack(std::string_view rel) {
    unmountPack();
    std::string path = assetPath(rel);
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) return MountResult::NotPresent;
    pack::Key key;
    detail::packKey(key.data());
    std::string error;
    mounted() = PackReader::open(io, key, error);
    pack::wipe(key.data(), key.size());
    if (!mounted()) {
        reportIntegrityFailure(("pack " + error).c_str());
        return MountResult::Corrupt;
    }
    return MountResult::Mounted;
}

bool packMounted() { return mounted() != nullptr; }

void unmountPack() { mounted().reset(); }

SDL_IOStream* openAsset(std::string_view rel) {
    if (PackReader* reader = mounted().get()) {
        const pack::Entry* entry = reader->find(rel);
        if (!entry) {
            SDL_SetError("Asset '%.*s' is not in the pack", int(rel.size()), rel.data());
            return nullptr;
        }
        std::string error;
        SDL_IOStream* io = reader->openEntry(*entry, error);
        if (!io) reportIntegrityFailure(error.c_str());
        return io;
    }
    std::string path = assetPath(rel);
    return SDL_IOFromFile(path.c_str(), "rb");
}

void setIntegrityFailureHandlerForTests(void (*handler)(const char* reason)) { testHandler() = handler; }

const pack::Entry* packEntryForTests(std::string_view path) {
    return mounted() ? mounted()->find(path) : nullptr;
}

} // namespace nova
