// folioedit :: KeyHex tests -- the 32-byte key <-> 64-char hex round-trip and
// strict rejection of malformed keys. PURE: links only KeyHex.cpp, no libcrypto,
// no nlohmann.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_keyhex.cpp ../src/KeyHex.cpp -o test_keyhex && ./test_keyhex
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_keyhex.cpp ../src/KeyHex.cpp -o test_keyhex && ./test_keyhex
*/

#include "folioedit/Seal.hpp"

#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}
static bool threw(const std::function<void()>& f) {
    try { f(); return false; } catch (const std::exception&) { return true; }
}

int main() {
    std::printf("folioedit KeyHex tests\n");

    // A known key <-> known hex, both directions.
    fe::bytes key(32);
    for (std::size_t i = 0; i < 32; ++i) key[i] = static_cast<std::uint8_t>(i);
    const std::string want =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    check("key_to_hex emits 64 lowercase hex chars", fe::key_to_hex(key) == want);
    check("hex_to_key inverts key_to_hex", fe::hex_to_key(want) == key);

    // Round-trip an all-0xFF key (high-nibble path).
    fe::bytes ff(32, 0xFF);
    check("0xFF key round-trips", fe::hex_to_key(fe::key_to_hex(ff)) == ff);

    // Uppercase hex is accepted on input, normalises to the same bytes.
    check("uppercase hex decodes identically",
          fe::hex_to_key("00" + std::string(62, 'F')) ==
          fe::hex_to_key("00" + std::string(62, 'f')));

    // Strict rejection.
    check("too-short hex is rejected", threw([]{ fe::hex_to_key(std::string(63, 'a')); }));
    check("too-long hex is rejected",  threw([]{ fe::hex_to_key(std::string(66, 'a')); }));
    check("non-hex char is rejected",
          threw([]{ fe::hex_to_key("zz" + std::string(62, 'a')); }));
    check("empty string is rejected",  threw([]{ fe::hex_to_key(""); }));

    // key_to_hex guards the byte length too.
    check("key_to_hex rejects a short key", threw([]{ fe::key_to_hex(fe::bytes(31)); }));
    check("key_to_hex rejects a long key",  threw([]{ fe::key_to_hex(fe::bytes(33)); }));

    std::printf("\nfolioedit keyhex: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
