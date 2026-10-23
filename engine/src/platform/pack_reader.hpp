#pragma once

#include "pack_format.hpp"

#include <SDL3/SDL_iostream.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace nova {

// Read side of a game.pak: authenticates the index once at open, then decrypts (and
// decompresses) one entry at a time on request. Not thread-safe; assets load on one thread.
class PackReader {
public:
    // Takes ownership of io (closed on failure too). nullptr plus error on a bad header or index.
    static std::unique_ptr<PackReader> open(SDL_IOStream* io, const pack::Key& key, std::string& error);
    ~PackReader();

    PackReader(const PackReader&) = delete;
    PackReader& operator=(const PackReader&) = delete;

    const pack::Entry* find(std::string_view path) const;
    // A memory stream holding the entry's original bytes, positioned at 0; the caller closes it.
    // nullptr plus error when the entry fails authentication or decompression.
    SDL_IOStream* openEntry(const pack::Entry& entry, std::string& error);

private:
    PackReader(SDL_IOStream* io, const pack::Key& key) : io_(io), key_(key) {}

    struct IoCloser {
        void operator()(SDL_IOStream* io) const { SDL_CloseIO(io); }
    };
    std::unique_ptr<SDL_IOStream, IoCloser> io_;
    pack::Key key_;
    std::vector<pack::Entry> entries_; // sorted by path (the tool writes them sorted; checked at open)
};

} // namespace nova
