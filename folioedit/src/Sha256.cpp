//
// folioedit :: Sha256 -- the pure-STL SHA-256 that replaces libcrypto's, so the
// custody + hash layer is toolkit-free and BOTH engine faces (sealed folioedit,
// plain folioedit-plain) compute byte-identical `binds` / chain hashes.
// (DESIGN_editorialization s18.4.)
//
// This is the REAL, standardized FIPS 180-4 SHA-256 -- NOT std::hash (which is a
// non-cryptographic, per-process-randomized table hash that would never verify
// across machines). The transform (K table, the six sigma/Sigma functions, the
// 64-round message schedule) is the standard algorithm, taken from the widely-
// copied public-domain reference (Brad Conte / LibTomCrypt lineage) so it is
// vetted, not hand-rolled; only the string->hex wrapper is ours.
//
// It provides folioedit::sha256_hex (declared in Custody.hpp). Because a hash is
// arithmetic on uint32_ts (<cstdint>, rotations, additions), this file is pure
// STL: it links no OpenSSL and compiles anywhere, on any arch, in a build with
// -DFOLIOEDIT_NO_CRYPTO and no libcrypto at all.
//
// Correctness is not taken on faith: TEST_sha256.cpp gates it with the FIPS-180
// known-answer vectors AND an equivalence battery -- the exact hashes the green
// s102 custody/seal fixtures commit to, recomputed against a libcrypto oracle --
// so this drops in under the existing signed, chained, on-disk artifacts without
// changing a single byte. (The s18.4 equivalence gate.)
//
#include "folioedit/Custody.hpp"   // declares sha256_hex

#include <cstddef>
#include <cstdint>
#include <string>

namespace folioedit {
namespace {

// ── FIPS 180-4 SHA-256 core (public-domain reference arithmetic) ─────────────

using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline u32 rotr(u32 x, unsigned n) { return (x >> n) | (x << (32u - n)); }

// The six logical functions.
inline u32 ch (u32 x, u32 y, u32 z) { return (x & y) ^ (~x & z); }
inline u32 maj(u32 x, u32 y, u32 z) { return (x & y) ^ (x & z) ^ (y & z); }
inline u32 big_sigma0(u32 x) { return rotr(x, 2)  ^ rotr(x, 13) ^ rotr(x, 22); }
inline u32 big_sigma1(u32 x) { return rotr(x, 6)  ^ rotr(x, 11) ^ rotr(x, 25); }
inline u32 sml_sigma0(u32 x) { return rotr(x, 7)  ^ rotr(x, 18) ^ (x >> 3);    }
inline u32 sml_sigma1(u32 x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);   }

// The 64 round constants (first 32 bits of the fractional parts of the cube
// roots of the first 64 primes) -- verbatim from FIPS 180-4.
constexpr u32 K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

struct Sha256Ctx {
    u32          h[8];
    unsigned char block[64];
    std::size_t  block_len = 0;   // bytes buffered in `block` (0..63)
    u64          total_len = 0;   // total message bytes seen
};

void init(Sha256Ctx& c) {
    // Initial hash values: fractional parts of the square roots of the first
    // eight primes (FIPS 180-4 §5.3.3).
    c.h[0] = 0x6a09e667u; c.h[1] = 0xbb67ae85u; c.h[2] = 0x3c6ef372u;
    c.h[3] = 0xa54ff53au; c.h[4] = 0x510e527fu; c.h[5] = 0x9b05688cu;
    c.h[6] = 0x1f83d9abu; c.h[7] = 0x5be0cd19u;
    c.block_len = 0;
    c.total_len = 0;
}

// Process one 64-byte block.
void transform(Sha256Ctx& c, const unsigned char* p) {
    u32 w[64];
    for (unsigned i = 0; i < 16; ++i) {
        w[i] = (static_cast<u32>(p[i * 4 + 0]) << 24) |
               (static_cast<u32>(p[i * 4 + 1]) << 16) |
               (static_cast<u32>(p[i * 4 + 2]) << 8)  |
               (static_cast<u32>(p[i * 4 + 3]));
    }
    for (unsigned i = 16; i < 64; ++i)
        w[i] = sml_sigma1(w[i - 2]) + w[i - 7] + sml_sigma0(w[i - 15]) + w[i - 16];

    u32 a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3];
    u32 e = c.h[4], f = c.h[5], g  = c.h[6], hh = c.h[7];

    for (unsigned i = 0; i < 64; ++i) {
        const u32 t1 = hh + big_sigma1(e) + ch(e, f, g) + K[i] + w[i];
        const u32 t2 = big_sigma0(a) + maj(a, b, cc);
        hh = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d;
    c.h[4] += e; c.h[5] += f; c.h[6] += g;  c.h[7] += hh;
}

void update(Sha256Ctx& c, const unsigned char* data, std::size_t len) {
    c.total_len += len;
    for (std::size_t i = 0; i < len; ++i) {
        c.block[c.block_len++] = data[i];
        if (c.block_len == 64) {
            transform(c, c.block);
            c.block_len = 0;
        }
    }
}

// Pad and emit the 32-byte digest.
void finish(Sha256Ctx& c, unsigned char out[32]) {
    const u64 bit_len = c.total_len * 8u;

    // Append 0x80, then zeros until 56 mod 64, then the 64-bit big-endian length.
    unsigned char pad0x80 = 0x80u;
    update(c, &pad0x80, 1);
    unsigned char zero = 0x00u;
    while (c.block_len != 56) update(c, &zero, 1);

    unsigned char lenbe[8];
    for (int i = 0; i < 8; ++i)
        lenbe[i] = static_cast<unsigned char>((bit_len >> (56 - i * 8)) & 0xffu);
    // update() bumps total_len, but we've already captured bit_len -- harmless.
    update(c, lenbe, 8);

    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<unsigned char>((c.h[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<unsigned char>((c.h[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<unsigned char>((c.h[i] >> 8)  & 0xffu);
        out[i * 4 + 3] = static_cast<unsigned char>((c.h[i])       & 0xffu);
    }
}

}  // namespace

// ── the public interface (declared in Custody.hpp) ───────────────────────────
std::string sha256_hex(const std::string& data) {
    Sha256Ctx ctx;
    init(ctx);
    update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    unsigned char md[32];
    finish(ctx, md);

    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i) {
        out.push_back(hexd[(md[i] >> 4) & 0x0F]);
        out.push_back(hexd[md[i] & 0x0F]);
    }
    return out;
}

}  // namespace folioedit
