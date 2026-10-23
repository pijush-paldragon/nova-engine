#pragma once

// One door for every asset read: from the encrypted game.pak when one is mounted, else the
// loose file under the asset root.

#include "pack_format.hpp"

#include <SDL3/SDL_iostream.h>

#include <cstdint>
#include <string_view>

namespace nova {

namespace detail {
// Defined by the build-generated key file (games) or support/test_pack_key.cpp (engine tests).
void packKey(std::uint8_t out[32]);
// Set by the game's entry point: true in pack builds, where a missing game.pak means tampering.
void setPackRequired(bool required);
bool packRequired();
} // namespace detail

enum class MountResult { Mounted, NotPresent, Corrupt };

// Opens <asset root>/<rel> as the asset pack. Corrupt means it exists but failed its header or
// index check (wrong key, modified, truncated); reportIntegrityFailure has then been called.
MountResult mountPack(std::string_view rel = "game.pak");
bool packMounted();
void unmountPack();

// The asset's bytes as a stream the caller closes, or nullptr when it doesn't exist. A pack entry
// that fails authentication calls reportIntegrityFailure and also yields nullptr.
SDL_IOStream* openAsset(std::string_view rel);

// A damaged or foreign pack: logged when NOVA_DEV_TOOLS is defined, then the process exits
// silently with status 1, unless a test handler is installed (it is called instead).
void reportIntegrityFailure(const char* reason);

#if defined(NOVA_TESTING)
void setIntegrityFailureHandlerForTests(void (*handler)(const char* reason));
const pack::Entry* packEntryForTests(std::string_view path);
#endif

} // namespace nova
