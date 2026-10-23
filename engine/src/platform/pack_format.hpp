#pragma once

// Nova asset pack format v1 (game.pak), shared by the nova_pack host tool and the engine.
// Standard-library types only, so the host tool compiles it without the rest of the engine.
//
// Layout (little-endian):
//   [0, 64)        header: magic "NOVAPAK\0", u32 version, u32 reserved, u64 indexOff,
//                  u64 indexSize, u8 indexNonce[24], u8 reserved[8]
//   [64, indexOff) entries: ciphertext followed by a 16-byte tag, one after another
//   [indexOff, …)  index ciphertext (indexSize bytes) + 16-byte tag
//
// Entries are XChaCha20-Poly1305 with the entry path as associated data; the index uses the
// 64 header bytes as associated data. Nonces are derived from the key and the content, so the
// same assets and key always produce a byte-identical pack.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nova::pack {

inline constexpr char kMagic[8] = {'N', 'O', 'V', 'A', 'P', 'A', 'K', '\0'};
inline constexpr uint32_t kVersion = 1;
inline constexpr size_t kHeaderSize = 64;
inline constexpr size_t kKeySize = 32;
inline constexpr size_t kNonceSize = 24;
inline constexpr size_t kTagSize = 16;
inline constexpr int kZstdLevel = 19;

enum Compression : uint8_t { kRaw = 0, kZstd = 1 };

using Key = std::array<uint8_t, kKeySize>;
using Nonce = std::array<uint8_t, kNonceSize>;

struct Header {
    uint64_t indexOff = 0;
    uint64_t indexSize = 0; // ciphertext bytes, without the tag
    Nonce indexNonce{};
};

std::array<uint8_t, kHeaderSize> encodeHeader(const Header& header);
// The index (plus its tag) sits after the header and ends exactly at the end of a fileSize-byte
// pack. Overflow-safe: the header is not authenticated yet when this is checked.
bool indexInRange(const Header& header, uint64_t fileSize);
// False on wrong magic/version or non-zero reserved bytes.
bool decodeHeader(const uint8_t* bytes, Header& out);

struct Entry {
    std::string path; // '/'-separated, relative to the assets root
    uint64_t dataOff = 0;
    uint32_t storedSize = 0; // ciphertext bytes, without the tag
    uint32_t originalSize = 0;
    uint8_t compression = kRaw;
    Nonce nonce{};
};

std::vector<uint8_t> encodeIndex(const std::vector<Entry>& entries);
// Bounds-checked; false on malformed input.
bool decodeIndex(const uint8_t* data, size_t size, std::vector<Entry>& out);

// First 24 bytes of keyed BLAKE2b-256 over domain || a || 0x00 || b.
Nonce deriveNonce(const Key& key, std::string_view domain, const uint8_t* a, size_t aLen, const uint8_t* b,
                  size_t bLen);

// An entry's nonce: deriveNonce over "nova-entry", the path, then the compression byte and the
// stored (encrypted) bytes. Binding the stored bytes means the same key never seals two different
// plaintexts under one nonce, even if zstd output for the same file changes between releases.
Nonce entryNonce(const Key& key, std::string_view path, uint8_t compression, const uint8_t* stored, size_t size);

void seal(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t size,
          uint8_t* cipherOut, uint8_t tagOut[kTagSize]);
// False if authentication fails (wrong key, modified data, wrong associated data).
bool open(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adLen, const uint8_t* cipher, size_t size,
          const uint8_t tag[kTagSize], uint8_t* plainOut);

// zstd at kZstdLevel; false when it wouldn't shrink the data to <= 95%.
bool compress(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
bool decompress(const uint8_t* data, size_t size, size_t originalSize, std::vector<uint8_t>& out);

bool parseKeyHex(std::string_view hex, Key& out);
std::string keyToHex(const Key& key);
void wipe(void* data, size_t size);

} // namespace nova::pack
