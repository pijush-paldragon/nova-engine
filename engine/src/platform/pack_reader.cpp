#include "pack_reader.hpp"

#include <algorithm>

namespace nova {
namespace {

bool readAt(SDL_IOStream* io, uint64_t offset, void* out, size_t size) {
    if (SDL_SeekIO(io, Sint64(offset), SDL_IO_SEEK_SET) != Sint64(offset)) return false;
    return size == 0 || SDL_ReadIO(io, out, size) == size;
}

} // namespace

std::unique_ptr<PackReader> PackReader::open(SDL_IOStream* io, const pack::Key& key, std::string& error) {
    std::unique_ptr<PackReader> reader(new PackReader(io, key));
    Sint64 fileSize = SDL_GetIOSize(io);
    uint8_t headerBytes[pack::kHeaderSize];
    pack::Header header;
    if (fileSize < Sint64(pack::kHeaderSize + pack::kTagSize) || !readAt(io, 0, headerBytes, sizeof headerBytes) ||
        !pack::decodeHeader(headerBytes, header)) {
        error = "bad header";
        return nullptr;
    }
    const uint64_t size = uint64_t(fileSize);
    if (!pack::indexInRange(header, size)) {
        error = "index out of range";
        return nullptr;
    }

    std::vector<uint8_t> index(size_t(header.indexSize) + pack::kTagSize);
    if (!readAt(io, header.indexOff, index.data(), index.size())) {
        error = "index unreadable";
        return nullptr;
    }
    uint8_t* tag = index.data() + header.indexSize;
    if (!pack::open(key, header.indexNonce, headerBytes, sizeof headerBytes, index.data(), size_t(header.indexSize),
                    tag, index.data())) {
        error = "index authentication failed";
        return nullptr;
    }
    if (!pack::decodeIndex(index.data(), size_t(header.indexSize), reader->entries_)) {
        error = "index malformed";
        return nullptr;
    }
    const std::vector<pack::Entry>& entries = reader->entries_;
    for (size_t i = 0; i < entries.size(); ++i) {
        const pack::Entry& e = entries[i];
        bool inRange = e.dataOff >= pack::kHeaderSize && e.dataOff <= header.indexOff &&
                       uint64_t(e.storedSize) + pack::kTagSize <= header.indexOff - e.dataOff;
        if (!inRange || (i > 0 && !(entries[i - 1].path < e.path))) {
            error = "index entry out of range or unsorted";
            return nullptr;
        }
    }
    return reader;
}

PackReader::~PackReader() { pack::wipe(key_.data(), key_.size()); }

const pack::Entry* PackReader::find(std::string_view path) const {
    auto it = std::lower_bound(entries_.begin(), entries_.end(), path,
                               [](const pack::Entry& e, std::string_view p) { return std::string_view(e.path) < p; });
    return it != entries_.end() && it->path == path ? &*it : nullptr;
}

SDL_IOStream* PackReader::openEntry(const pack::Entry& entry, std::string& error) {
    std::vector<uint8_t> stored(size_t(entry.storedSize) + pack::kTagSize);
    if (!readAt(io_.get(), entry.dataOff, stored.data(), stored.size())) {
        error = "entry unreadable: " + entry.path;
        return nullptr;
    }
    const auto* ad = reinterpret_cast<const uint8_t*>(entry.path.data());
    if (!pack::open(key_, entry.nonce, ad, entry.path.size(), stored.data(), entry.storedSize,
                    stored.data() + entry.storedSize, stored.data())) {
        error = "entry authentication failed: " + entry.path;
        return nullptr;
    }
    stored.resize(entry.storedSize);
    std::vector<uint8_t> original;
    if (entry.compression == pack::kZstd) {
        if (!pack::decompress(stored.data(), stored.size(), entry.originalSize, original)) {
            error = "entry decompression failed: " + entry.path;
            return nullptr;
        }
    } else if (entry.storedSize == entry.originalSize) {
        original.swap(stored);
    } else {
        error = "entry size mismatch: " + entry.path;
        return nullptr;
    }

    SDL_IOStream* mem = SDL_IOFromDynamicMem();
    if (!mem) {
        error = SDL_GetError();
        return nullptr;
    }
    // One chunk the size of the asset, so the stream allocates exactly once.
    SDL_SetNumberProperty(SDL_GetIOProperties(mem), SDL_PROP_IOSTREAM_DYNAMIC_CHUNKSIZE_NUMBER,
                          Sint64(std::max<size_t>(original.size(), 1)));
    if ((!original.empty() && SDL_WriteIO(mem, original.data(), original.size()) != original.size()) ||
        SDL_SeekIO(mem, 0, SDL_IO_SEEK_SET) != 0) {
        error = SDL_GetError();
        SDL_CloseIO(mem);
        return nullptr;
    }
    return mem;
}

} // namespace nova
