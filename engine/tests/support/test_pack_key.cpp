// The engine tests' pack key: the same 64 hex characters as data/test.key, which builds the test pack.
#include <cstdint>

namespace nova::detail {

void packKey(std::uint8_t out[32]) {
    static const char kHex[] = "04967f470629360405f0d3785e26e26a2ba674ace5eee439471ef50aabc1cc16";
    auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (int i = 0; i < 32; ++i) out[i] = std::uint8_t(nibble(kHex[2 * i]) << 4 | nibble(kHex[2 * i + 1]));
}

} // namespace nova::detail
