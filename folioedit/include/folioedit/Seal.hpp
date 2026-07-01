#pragma once
//
// folioedit :: Seal -- the crypto seam. bytes in -> sealed Envelope; Envelope +
// key -> bytes out. AES-256-GCM via libcrypto EVP, implemented ENTIRELY in
// Seal_openssl.cpp -- the only translation unit that includes <openssl/*>. No
// EVP / OpenSSL types appear in this header, so the format + custody layers and
// their tests never pull in OpenSSL.
//
// The key is a raw 32-byte AES-256 key. Deriving it from a passphrase + the
// envelope salt (the "give someone the key to open it" ergonomics) is a separate
// KDF step layered above this seam. (DESIGN_editorialization s16.1.)
//
#include <string>

#include "folioedit/Envelope.hpp"

namespace folioedit {

// The linked crypto backend's version string (e.g. "OpenSSL 3.5.7 9 Jun 2026").
// Returns a plain string -- no OpenSSL types leak. Lets a build confirm the
// libcrypto link + version end to end without running a real seal.
std::string seal_backend();

// ── key <-> hex (the travelling form) ────────────────────────────────────────
// A .folioedit key is 32 raw bytes, but bytes don't travel: Folio shows the key
// and the editor pastes it into `-k` as a 64-char lowercase hex string. The
// engine converts at the door -- hex is the surface, bytes are internal. Pure
// (no crypto), so it's defined in the dependency-free KeyHex.cpp and testable
// without libcrypto.
std::string key_to_hex(const bytes& key);        // 32 bytes -> 64 lowercase hex chars
bytes       hex_to_key(const std::string& hex);  // 64 hex chars -> 32 bytes; strict, throws

// Seal plaintext under a 32-byte key. Generates a fresh random nonce, runs
// AES-256-GCM, returns the Envelope (nonce + ciphertext + tag populated; salt is
// set by the caller/KDF layer). Throws on a crypto failure.
Envelope seal(const bytes& plaintext, const bytes& key);

// Verify + decrypt an Envelope. GCM tag failure (tampered file / wrong key)
// throws -- there is no "partial" open. Returns the recovered plaintext.
bytes unseal(const Envelope& env, const bytes& key);

// ── passphrase KDF layer (above the raw seam) ────────────────────────────────
// Turns the raw-32-byte-key seal into the "give someone a passphrase to open it"
// ergonomics the design wants (DESIGN_editorialization s16.1). PBKDF2-HMAC-SHA256
// over the envelope salt; the raw seal/unseal below stay untouched -- this only
// derives a key and records how, so the raw seam's tested AAD is unchanged.

// Default PBKDF2 iteration count. High by design: this is the wall between a
// stolen sealed file and an offline passphrase guess. Recorded per-file
// (envelope kdf_iters), so it can be raised later without breaking old files.
inline constexpr std::uint32_t KDF_DEFAULT_ITERS = 600000;

// Derive a raw 32-byte AES-256 key from a passphrase + salt via PBKDF2-HMAC-
// SHA256. Deterministic: same passphrase + salt + iters -> same key. Throws on
// an empty salt or an out-of-range iteration count.
bytes derive_key(const std::string& passphrase, const bytes& salt, std::uint32_t iters);

// Passphrase ergonomics over seal(): generate a fresh random salt, derive the
// key, seal. The returned Envelope records kdf_id + kdf_iters + salt so
// unseal_with_passphrase can re-derive. Throws on a crypto failure.
Envelope seal_with_passphrase(const bytes& plaintext, const std::string& passphrase,
                              std::uint32_t iters = KDF_DEFAULT_ITERS);

// Re-derive the key from the Envelope's recorded salt + iters, then unseal. A
// wrong passphrase yields a wrong key -> the GCM tag fails -> throws (no partial
// open). Throws if the envelope carries no PBKDF2 KDF params.
bytes unseal_with_passphrase(const Envelope& env, const std::string& passphrase);

}  // namespace folioedit
