//
// folioedit :: Identity_openssl -- Ed25519 keypairs + event signing via
// libcrypto EVP_PKEY. The impl-side companion to Identity.hpp; the only Identity
// TU that includes <openssl/*> (mirrors Seal_openssl.cpp).
//
// Ed25519 is a one-shot signature (no streaming digest): EVP_DigestSign /
// EVP_DigestVerify with a null message digest. Keys are raw 32-byte values in /
// out of EVP_PKEY via the get/new_raw_{private,public}_key calls. Non-deprecated
// EVP throughout (OpenSSL 3 deprecates the low-level ED25519_* calls).
//
// Key persistence reuses the 32-byte key<->hex codec from Seal.hpp (KeyHex.cpp),
// so there is one strict hex decoder for keys across the engine.
//
#include "folioedit/Identity.hpp"
#include "folioedit/Custody.hpp"   // sha256_hex (fingerprint)
#include "folioedit/Seal.hpp"      // key_to_hex / hex_to_key (persistence)

#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>

namespace folioedit {
namespace {

constexpr std::size_t ED25519_KEY_LEN = 32;   // raw seed / public key
constexpr std::size_t ED25519_SIG_LEN = 64;   // signature

using PkeyPtr    = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using MdCtxPtr   = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

// ── local hex (arbitrary length; keys use KeyHex.cpp, signatures use these) ───
std::string to_hex(const key_bytes& b) {
    static const char* h = "0123456789abcdef";
    std::string out;
    out.reserve(b.size() * 2);
    for (std::uint8_t c : b) {
        out.push_back(h[(c >> 4) & 0x0F]);
        out.push_back(h[c & 0x0F]);
    }
    return out;
}
int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Decode hex -> bytes. Returns false (not throw) on malformed input, so a bad
// signature verifies as false rather than raising.
bool hex_decode(const std::string& hex, key_bytes& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

PkeyPtr private_pkey(const key_bytes& priv) {
    if (priv.size() != ED25519_KEY_LEN)
        throw std::runtime_error("folioedit: Ed25519 private key must be 32 bytes");
    PkeyPtr pk(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                            priv.data(), priv.size()),
               &EVP_PKEY_free);
    if (!pk) throw std::runtime_error("folioedit: load Ed25519 private key failed");
    return pk;
}
PkeyPtr public_pkey(const key_bytes& pub) {
    if (pub.size() != ED25519_KEY_LEN)
        throw std::runtime_error("folioedit: Ed25519 public key must be 32 bytes");
    PkeyPtr pk(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                           pub.data(), pub.size()),
               &EVP_PKEY_free);
    if (!pk) throw std::runtime_error("folioedit: load Ed25519 public key failed");
    return pk;
}

}  // namespace

KeyPair generate_keypair() {
    PkeyCtxPtr ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr), &EVP_PKEY_CTX_free);
    if (!ctx) throw std::runtime_error("folioedit: EVP_PKEY_CTX_new_id (ed25519) failed");
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
        throw std::runtime_error("folioedit: EVP_PKEY_keygen_init failed");

    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw) <= 0)
        throw std::runtime_error("folioedit: EVP_PKEY_keygen (ed25519) failed");
    PkeyPtr pkey(raw, &EVP_PKEY_free);

    KeyPair kp;
    kp.private_key.resize(ED25519_KEY_LEN);
    kp.public_key.resize(ED25519_KEY_LEN);

    std::size_t plen = ED25519_KEY_LEN;
    if (EVP_PKEY_get_raw_private_key(pkey.get(), kp.private_key.data(), &plen) != 1
        || plen != ED25519_KEY_LEN)
        throw std::runtime_error("folioedit: extract raw private key failed");

    std::size_t klen = ED25519_KEY_LEN;
    if (EVP_PKEY_get_raw_public_key(pkey.get(), kp.public_key.data(), &klen) != 1
        || klen != ED25519_KEY_LEN)
        throw std::runtime_error("folioedit: extract raw public key failed");

    return kp;
}

std::string fingerprint(const key_bytes& public_key) {
    if (public_key.size() != ED25519_KEY_LEN)
        throw std::runtime_error("folioedit: fingerprint needs a 32-byte public key");
    return sha256_hex(std::string(public_key.begin(), public_key.end()));
}

std::string sign_hash(const key_bytes& private_key, const std::string& hash_hex) {
    PkeyPtr pkey = private_pkey(private_key);

    MdCtxPtr md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!md) throw std::runtime_error("folioedit: EVP_MD_CTX_new failed");
    // Ed25519: null digest, one-shot.
    if (EVP_DigestSignInit(md.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
        throw std::runtime_error("folioedit: DigestSignInit (ed25519) failed");

    const auto* msg = reinterpret_cast<const unsigned char*>(hash_hex.data());
    std::size_t siglen = 0;
    if (EVP_DigestSign(md.get(), nullptr, &siglen, msg, hash_hex.size()) != 1)
        throw std::runtime_error("folioedit: DigestSign (size probe) failed");

    key_bytes sig(siglen);
    if (EVP_DigestSign(md.get(), sig.data(), &siglen, msg, hash_hex.size()) != 1)
        throw std::runtime_error("folioedit: DigestSign failed");
    sig.resize(siglen);
    return to_hex(sig);
}

bool verify_hash(const key_bytes& public_key, const std::string& hash_hex,
                 const std::string& signature_hex) {
    key_bytes sig;
    if (!hex_decode(signature_hex, sig)) return false;
    if (sig.size() != ED25519_SIG_LEN)   return false;

    PkeyPtr pkey = public_pkey(public_key);

    MdCtxPtr md(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!md) throw std::runtime_error("folioedit: EVP_MD_CTX_new failed");
    if (EVP_DigestVerifyInit(md.get(), nullptr, nullptr, nullptr, pkey.get()) != 1)
        throw std::runtime_error("folioedit: DigestVerifyInit (ed25519) failed");

    const auto* msg = reinterpret_cast<const unsigned char*>(hash_hex.data());
    // EVP_DigestVerify returns 1 == ok, 0 == bad signature, <0 == error.
    return EVP_DigestVerify(md.get(), sig.data(), sig.size(), msg, hash_hex.size()) == 1;
}

void sign_event(CustodyEvent& e, const KeyPair& kp) {
    if (e.hash.empty())
        throw std::runtime_error("folioedit: sign_event needs a finalized event (empty hash)");
    // actor_id is a BOUND field -- it must already be the signer's fingerprint,
    // set before finalize_event, so the signed hash commits to the identity.
    // Signing must not mutate any bound field (that would invalidate e.hash).
    const std::string fp = fingerprint(kp.public_key);
    if (e.actor_id.empty())
        throw std::runtime_error(
            "folioedit: sign_event: event actor_id must be set to the signer's "
            "fingerprint before finalize (so the hash binds the identity)");
    if (e.actor_id != fp)
        throw std::runtime_error(
            "folioedit: sign_event: signing key fingerprint does not match event actor_id");
    e.signature = sign_hash(kp.private_key, e.hash);
}

bool verify_event_signature(const CustodyEvent& e, const key_bytes& public_key) {
    if (e.signature.empty()) return false;
    return verify_hash(public_key, e.hash, e.signature);
}

// ── persistence ──────────────────────────────────────────────────────────────
void save_keypair(const KeyPair& kp, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("folioedit: cannot open key file for writing: " + path);
    f << key_to_hex(kp.private_key) << '\n' << key_to_hex(kp.public_key) << '\n';
    if (!f) throw std::runtime_error("folioedit: failed writing key file: " + path);
}

KeyPair load_keypair(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("folioedit: cannot open key file for reading: " + path);
    std::string priv_hex, pub_hex;
    if (!std::getline(f, priv_hex) || !std::getline(f, pub_hex))
        throw std::runtime_error("folioedit: malformed key file (expected two hex lines): " + path);
    KeyPair kp;
    kp.private_key = hex_to_key(priv_hex);   // strict: 64 hex chars -> 32 bytes
    kp.public_key  = hex_to_key(pub_hex);
    return kp;
}

}  // namespace folioedit
