#include "pack_format.hpp"

#include <monocypher.h>
#include <zstd.h>

#include <cstring>

#if !defined(NOVA_PACK_HOST_TOOL)
// The engine only links zstd's decompressor; compression exists in the host tool.
#define NOVA_PACK_NO_COMPRESS 1
#endif

namespace nova::pack {
namespace {

void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
void put64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i));
}
uint32_t get32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i);
    return v;
}
uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (8 * i);
    return v;
}

} // namespace

std::array<uint8_t, kHeaderSize> encodeHeader(const Header& h) {
    std::array<uint8_t, kHeaderSize> out{};
    std::memcpy(out.data(), kMagic, 8);
    put32(out.data() + 8, kVersion);
    put32(out.data() + 12, 0);
    put64(out.data() + 16, h.indexOff);
    put64(out.data() + 24, h.indexSize);
    std::memcpy(out.data() + 32, h.indexNonce.data(), kNonceSize);
    return out; // bytes 56..63 stay zero (reserved)
}

bool decodeHeader(const uint8_t* b, Header& out) {
    if (std::memcmp(b, kMagic, 8) != 0 || get32(b + 8) != kVersion || get32(b + 12) != 0) return false;
    for (size_t i = 56; i < kHeaderSize; ++i) {
        if (b[i] != 0) return false;
    }
    out.indexOff = get64(b + 16);
    out.indexSize = get64(b + 24);
    std::memcpy(out.indexNonce.data(), b + 32, kNonceSize);
    return true;
}

std::vector<uint8_t> encodeIndex(const std::vector<Entry>& entries) {
    std::vector<uint8_t> out(4);
    put32(out.data(), uint32_t(entries.size()));
    for (const Entry& e : entries) {
        size_t at = out.size();
        out.resize(at + 2 + e.path.size() + 8 + 4 + 4 + 1 + kNonceSize);
        uint8_t* p = out.data() + at;
        p[0] = uint8_t(e.path.size());
        p[1] = uint8_t(e.path.size() >> 8);
        std::memcpy(p + 2, e.path.data(), e.path.size());
        p += 2 + e.path.size();
        put64(p, e.dataOff);
        put32(p + 8, e.storedSize);
        put32(p + 12, e.originalSize);
        p[16] = e.compression;
        std::memcpy(p + 17, e.nonce.data(), kNonceSize);
    }
    return out;
}

bool decodeIndex(const uint8_t* data, size_t size, std::vector<Entry>& out) {
    out.clear();
    if (size < 4) return false;
    uint32_t count = get32(data);
    size_t at = 4;
    constexpr size_t kFixed = 8 + 4 + 4 + 1 + kNonceSize;
    if (count > size / (2 + kFixed)) return false; // can't possibly fit
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (size - at < 2) return false;
        size_t len = size_t(data[at]) | (size_t(data[at + 1]) << 8);
        at += 2;
        if (size - at < len + kFixed) return false;
        Entry e;
        e.path.assign(reinterpret_cast<const char*>(data + at), len);
        at += len;
        e.dataOff = get64(data + at);
        e.storedSize = get32(data + at + 8);
        e.originalSize = get32(data + at + 12);
        e.compression = data[at + 16];
        if (e.compression != kRaw && e.compression != kZstd) return false;
        std::memcpy(e.nonce.data(), data + at + 17, kNonceSize);
        at += kFixed;
        out.push_back(std::move(e));
    }
    return at == size;
}

Nonce deriveNonce(const Key& key, std::string_view domain, const uint8_t* a, size_t aLen, const uint8_t* b,
                  size_t bLen) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 32, key.data(), key.size());
    crypto_blake2b_update(&ctx, reinterpret_cast<const uint8_t*>(domain.data()), domain.size());
    crypto_blake2b_update(&ctx, a, aLen);
    const uint8_t zero = 0;
    crypto_blake2b_update(&ctx, &zero, 1);
    crypto_blake2b_update(&ctx, b, bLen);
    uint8_t hash[32];
    crypto_blake2b_final(&ctx, hash);
    Nonce nonce;
    std::memcpy(nonce.data(), hash, kNonceSize);
    crypto_wipe(hash, sizeof hash);
    return nonce;
}

void seal(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adLen, const uint8_t* plain, size_t size,
          uint8_t* cipherOut, uint8_t tagOut[kTagSize]) {
    crypto_aead_lock(cipherOut, tagOut, key.data(), nonce.data(), ad, adLen, plain, size);
}

bool open(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adLen, const uint8_t* cipher, size_t size,
          const uint8_t tag[kTagSize], uint8_t* plainOut) {
    return crypto_aead_unlock(plainOut, tag, key.data(), nonce.data(), ad, adLen, cipher, size) == 0;
}

bool compress(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
#if defined(NOVA_PACK_NO_COMPRESS)
    (void)data;
    (void)size;
    (void)out;
    return false;
#else
    out.resize(ZSTD_compressBound(size));
    size_t n = ZSTD_compress(out.data(), out.size(), data, size, kZstdLevel);
    if (ZSTD_isError(n) || n * 100 > size * 95) return false;
    out.resize(n);
    return true;
#endif
}

bool decompress(const uint8_t* data, size_t size, size_t originalSize, std::vector<uint8_t>& out) {
    out.resize(originalSize);
    size_t n = ZSTD_decompress(out.data(), out.size(), data, size);
    return !ZSTD_isError(n) && n == originalSize;
}

bool parseKeyHex(std::string_view hex, Key& out) {
    while (!hex.empty() && (hex.back() == '\n' || hex.back() == '\r' || hex.back() == ' ')) hex.remove_suffix(1);
    if (hex.size() != kKeySize * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < kKeySize; ++i) {
        int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return true;
}

std::string keyToHex(const Key& key) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(kKeySize * 2);
    for (uint8_t b : key) {
        s += digits[b >> 4];
        s += digits[b & 15];
    }
    return s;
}

void wipe(void* data, size_t size) { crypto_wipe(data, size); }

} // namespace nova::pack
