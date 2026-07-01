#pragma once
//
// folioedit :: Envelope -- the on-disk framing around the AES-256-GCM seal.
//
// This is the OUTER wrapper (the part that is NOT encrypted): magic + versions
// + the KDF salt + the GCM nonce + the ciphertext + the GCM tag. The plaintext
// that gets sealed (body + annotations + custody) lives in Format.hpp; the seal
// itself is Seal.hpp. Framing here is pure -- no OpenSSL types, no crypto -- so
// this header is safe to include anywhere, including OpenSSL-free tests.
//
// cipher_id is a versioned byte so the cipher can be swapped without breaking
// old files (DESIGN_editorialization s16.1). Today only AES-256-GCM exists.
//
#include <cstdint>
#include <string>
#include <vector>

namespace folioedit {

using bytes = std::vector<std::uint8_t>;

inline constexpr int         SCHEMA_VERSION = 1;            // folioedit_schema
inline constexpr const char* MAGIC          = "FOLIOEDIT";  // leading file magic

enum class CipherId : std::uint8_t {
    AesGcm256 = 1,   // AES-256-GCM: authenticated, tag = integrity/drift check
};

// How the raw 32-byte key was produced. `None` = a raw key was handed straight
// to seal() (salt empty, no derivation). Otherwise a passphrase was stretched
// over `salt` by this KDF, and unseal re-derives from salt + kdf_iters. Versioned
// so a stronger KDF (e.g. Argon2id) can be added as id 2 without breaking old
// files. (DESIGN_editorialization s16.1.)
enum class KdfId : std::uint8_t {
    None             = 0,   // raw-key seal, no passphrase derivation
    Pbkdf2HmacSha256 = 1,   // PBKDF2-HMAC-SHA256 over the envelope salt
};

struct Envelope {
    std::string magic  = MAGIC;
    int         schema = SCHEMA_VERSION;
    CipherId    cipher = CipherId::AesGcm256;
    KdfId       kdf_id    = KdfId::None;   // how salt+passphrase -> key (if any)
    std::uint32_t kdf_iters = 0;           // KDF work factor (0 when kdf_id None)
    bytes       salt;         // KDF salt (passphrase -> key)
    bytes       nonce;        // 12-byte GCM IV, unique per seal
    bytes       ciphertext;   // sealed plaintext
    bytes       tag;          // 16-byte GCM authentication tag
};

// Pure framing: serialise an Envelope to a flat byte buffer and back. No crypto
// happens here -- these only lay out / read the wrapper fields. (Defined in
// Format.cpp -- pure, sandbox-testable without OpenSSL.)
bytes    envelope_to_bytes(const Envelope& env);
Envelope envelope_from_bytes(const bytes& raw);

}  // namespace folioedit
