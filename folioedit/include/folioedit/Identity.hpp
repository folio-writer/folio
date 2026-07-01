#pragma once
//
// folioedit :: Identity -- the "who": TOFU Ed25519 keypairs and event signing.
//
// A signature only proves "the holder of private key K signed this event hash."
// TOFU makes K mean an editor: they generate their own keypair once, hand over
// the PUBLIC key, and their actor_id is that public key's fingerprint. Every pass
// they seal signs the custody event's hash with their PRIVATE key; import verifies
// against the pinned public key. This is the piece the shared seal key cannot do
// -- both author and editor know the passphrase, so possession proves nothing
// about who. Asymmetric keys are what carry "whose hand." (s16.4.)
//
// Ed25519 via libcrypto EVP_PKEY, entirely in Identity_openssl.cpp -- no OpenSSL
// types appear in this header, matching the Seal.hpp discipline. The signature
// is over the event's `hash`, which already commits (via the canonical contents
// + prev_hash chain) to every bound field, so signing the hash signs the event.
//
#include <cstdint>
#include <string>
#include <vector>

#include "folioedit/Custody.hpp"

namespace folioedit {

using key_bytes = std::vector<std::uint8_t>;   // raw key / signature material

struct KeyPair {
    key_bytes private_key;   // raw 32-byte Ed25519 seed (the secret; never travels)
    key_bytes public_key;    // raw 32-byte Ed25519 public key (handed over once)
};

// Generate a fresh Ed25519 keypair from the CSPRNG. Throws on crypto failure.
KeyPair generate_keypair();

// actor_id = SHA-256 fingerprint (lowercase hex) of the raw public key. Stable,
// offline, no authority -- the TOFU handle an editor is known by.
std::string fingerprint(const key_bytes& public_key);

// -- primitives (over a hash-hex string) --------------------------------------
// Sign a hash-hex string with a private key -> signature as lowercase hex.
std::string sign_hash(const key_bytes& private_key, const std::string& hash_hex);

// Verify a signature (hex) over a hash-hex against a public key. Returns false on
// any mismatch or malformed signature -- it never throws for a bad signature
// (only for a misused/wrong-length key).
bool verify_hash(const key_bytes& public_key, const std::string& hash_hex,
                 const std::string& signature_hex);

// -- CustodyEvent-level convenience -------------------------------------------
// Sign a FINALIZED event (needs e.hash). Its actor_id must ALREADY be the
// signer's fingerprint -- set actor_id = fingerprint(public) BEFORE
// finalize_event, so the hash (and thus the signature) commits to the identity.
// sign_event only sets e.signature; it never mutates a bound field. Throws if the
// event is unfinalized or if the signing key's fingerprint != e.actor_id.
void sign_event(CustodyEvent& e, const KeyPair& kp);

// Verify one event's signature against a public key. Pair with verify_chain:
// verify_chain proves the hash is intact + ordered; this proves the hash was
// signed by that key. Together they defeat "repair the hash after tampering,"
// because re-signing needs the private key. Returns false if unsigned/mismatched.
bool verify_event_signature(const CustodyEvent& e, const key_bytes& public_key);

// -- persistence (the editor's local identity store) --------------------------
// Save / load a keypair as hex text (private hex on line 1, public hex on line 2).
// Pure STL file I/O. Throws on an I/O error or a malformed file.
void    save_keypair(const KeyPair& kp, const std::string& path);
KeyPair load_keypair(const std::string& path);

}  // namespace folioedit
