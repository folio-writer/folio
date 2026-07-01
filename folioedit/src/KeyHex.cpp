//
// folioedit :: KeyHex -- the 32-byte key <-> 64-char hex conversion at the door.
//
// Deliberately dependency-free (STL only, NO libcrypto, NO nlohmann): the key's
// travelling form is pure byte<->text, and keeping it in its own tiny TU means
// Folio, the CLI, and its test can share one strict decoder without dragging in
// the crypto or JSON layers. (DESIGN_editorialization s16.1 -- "a key opens it";
// the hex string is what a human actually copies.)
//
// Strict on input: exactly 64 chars, hex only, or it throws -- a mistyped key
// must fail loudly, never silently decode to the wrong bytes.
//
#include "folioedit/Seal.hpp"

#include <stdexcept>

namespace folioedit {
namespace {

constexpr std::size_t KEY_BYTES = 32;   // AES-256
constexpr std::size_t HEX_CHARS = 64;   // 2 hex digits per byte

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;   // accept upper on input
    return -1;
}

}  // namespace

std::string key_to_hex(const bytes& key) {
    if (key.size() != KEY_BYTES)
        throw std::runtime_error("folioedit: key_to_hex needs a 32-byte key");
    static const char* h = "0123456789abcdef";
    std::string out;
    out.reserve(HEX_CHARS);
    for (std::uint8_t b : key) {
        out.push_back(h[(b >> 4) & 0x0F]);
        out.push_back(h[b & 0x0F]);
    }
    return out;
}

bytes hex_to_key(const std::string& hex) {
    if (hex.size() != HEX_CHARS)
        throw std::runtime_error("folioedit: a key must be exactly 64 hex chars (32 bytes)");
    bytes key;
    key.reserve(KEY_BYTES);
    for (std::size_t i = 0; i < HEX_CHARS; i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            throw std::runtime_error("folioedit: key contains a non-hex character");
        key.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return key;
}

}  // namespace folioedit
