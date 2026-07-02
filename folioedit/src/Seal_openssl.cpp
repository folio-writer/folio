//
// folioedit :: Seal_openssl -- the ONLY translation unit that includes
// <openssl/*>. AES-256-GCM seal/unseal via libcrypto EVP.
//
// Design (reviewed before build):
//  - fresh random 12-byte nonce per seal (RAND_bytes) -- GCM must never reuse a
//    nonce under one key; a whole-file re-seal every pass makes fresh-random right.
//  - 16-byte GCM tag: GET_TAG after seal, SET_TAG before final on unseal.
//  - the envelope header (magic + schema + cipher) is bound as AAD, so the tag
//    covers the framing too -- cipher_id / schema can't be swapped undetected.
//  - raw 32-byte key enforced; the passphrase->key KDF is a separate layer.
//
// Non-deprecated EVP throughout (OpenSSL 3 deprecates the low-level calls).
// GCM produces no bytes at Final, so the AAD update's length output is kept in
// its own variable and Final writes into a scratch buffer -- never into an
// offset of a possibly-empty ciphertext/plaintext buffer.
//
#include "folioedit/Seal.hpp"
#include "folioedit/Passphrase.hpp"   // canonicalize_passphrase -- the phrase->key seam

#include <climits>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace folioedit {
namespace {

constexpr int NONCE_LEN = 12;   // GCM standard IV
constexpr int TAG_LEN   = 16;   // GCM tag
constexpr int KEY_LEN   = 32;   // AES-256
constexpr int SALT_LEN  = 16;   // PBKDF2 salt (fresh random per seal)

using CtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
CtxPtr new_ctx() {
    CtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx) throw std::runtime_error("folioedit: EVP_CIPHER_CTX_new failed");
    return ctx;
}

// The bytes the tag authenticates in addition to the ciphertext: the framing
// header. Built identically on seal + unseal so any header edit fails the tag.
bytes header_aad(int schema, CipherId cipher) {
    bytes aad;
    const char* m = MAGIC;
    aad.insert(aad.end(), m, m + std::strlen(m));
    for (int i = 0; i < 4; ++i)
        aad.push_back(static_cast<std::uint8_t>((static_cast<unsigned>(schema) >> (8 * i)) & 0xFF));
    aad.push_back(static_cast<std::uint8_t>(cipher));
    return aad;
}

int to_int_len(std::size_t n, const char* what) {
    if (n > static_cast<std::size_t>(INT_MAX))
        throw std::runtime_error(std::string("folioedit: ") + what + " too large");
    return static_cast<int>(n);
}

}  // namespace

std::string seal_backend() {
    return OpenSSL_version(OPENSSL_VERSION);
}

Envelope seal(const bytes& plaintext, const bytes& key) {
    if (key.size() != static_cast<std::size_t>(KEY_LEN))
        throw std::runtime_error("folioedit: seal() needs a 32-byte AES-256 key");

    Envelope env;
    env.magic  = MAGIC;
    env.schema = SCHEMA_VERSION;
    env.cipher = CipherId::AesGcm256;

    env.nonce.resize(static_cast<std::size_t>(NONCE_LEN));
    if (RAND_bytes(env.nonce.data(), NONCE_LEN) != 1)
        throw std::runtime_error("folioedit: RAND_bytes (nonce) failed");

    const bytes aad = header_aad(env.schema, env.cipher);

    CtxPtr ctx = new_ctx();
    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        throw std::runtime_error("folioedit: EncryptInit (cipher) failed");
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, nullptr) != 1)
        throw std::runtime_error("folioedit: set GCM IV length failed");
    if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), env.nonce.data()) != 1)
        throw std::runtime_error("folioedit: EncryptInit (key/iv) failed");

    int aad_len = 0;   // AAD update's length output -- kept out of the ct path
    if (EVP_EncryptUpdate(ctx.get(), nullptr, &aad_len, aad.data(),
                          to_int_len(aad.size(), "aad")) != 1)
        throw std::runtime_error("folioedit: EncryptUpdate (aad) failed");

    env.ciphertext.resize(plaintext.size());
    int ct_len = 0;
    if (!plaintext.empty()) {
        if (EVP_EncryptUpdate(ctx.get(), env.ciphertext.data(), &ct_len, plaintext.data(),
                              to_int_len(plaintext.size(), "plaintext")) != 1)
            throw std::runtime_error("folioedit: EncryptUpdate (plaintext) failed");
    }

    unsigned char fin[EVP_MAX_BLOCK_LENGTH];
    int final_len = 0;
    if (EVP_EncryptFinal_ex(ctx.get(), fin, &final_len) != 1)  // GCM: final_len == 0
        throw std::runtime_error("folioedit: EncryptFinal failed");
    (void)ct_len;
    (void)final_len;

    env.tag.resize(static_cast<std::size_t>(TAG_LEN));
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_LEN, env.tag.data()) != 1)
        throw std::runtime_error("folioedit: get GCM tag failed");

    return env;
}

bytes unseal(const Envelope& env, const bytes& key) {
    if (key.size() != static_cast<std::size_t>(KEY_LEN))
        throw std::runtime_error("folioedit: unseal() needs a 32-byte AES-256 key");
    if (env.cipher != CipherId::AesGcm256)
        throw std::runtime_error("folioedit: unsupported cipher_id");
    if (env.nonce.size() != static_cast<std::size_t>(NONCE_LEN))
        throw std::runtime_error("folioedit: bad nonce length");
    if (env.tag.size() != static_cast<std::size_t>(TAG_LEN))
        throw std::runtime_error("folioedit: bad tag length");

    const bytes aad = header_aad(env.schema, env.cipher);

    CtxPtr ctx = new_ctx();
    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
        throw std::runtime_error("folioedit: DecryptInit (cipher) failed");
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, nullptr) != 1)
        throw std::runtime_error("folioedit: set GCM IV length failed");
    if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), env.nonce.data()) != 1)
        throw std::runtime_error("folioedit: DecryptInit (key/iv) failed");

    int aad_len = 0;
    if (EVP_DecryptUpdate(ctx.get(), nullptr, &aad_len, aad.data(),
                          to_int_len(aad.size(), "aad")) != 1)
        throw std::runtime_error("folioedit: DecryptUpdate (aad) failed");

    bytes plaintext(env.ciphertext.size());
    int pt_len = 0;
    if (!env.ciphertext.empty()) {
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &pt_len, env.ciphertext.data(),
                              to_int_len(env.ciphertext.size(), "ciphertext")) != 1)
            throw std::runtime_error("folioedit: DecryptUpdate (ciphertext) failed");
    }

    // Set the expected tag, then final: this IS the authentication check. A
    // tampered ciphertext / tag / AAD-header makes final return <= 0.
    bytes tag = env.tag;   // SET_TAG wants a non-const buffer
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_LEN, tag.data()) != 1)
        throw std::runtime_error("folioedit: set GCM tag failed");

    unsigned char fin[EVP_MAX_BLOCK_LENGTH];
    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), fin, &final_len) <= 0)  // GCM: final_len == 0
        throw std::runtime_error("folioedit: seal verification failed (tampered or wrong key)");

    plaintext.resize(static_cast<std::size_t>(pt_len + final_len));  // == ciphertext size
    return plaintext;
}

// ── passphrase KDF layer ─────────────────────────────────────────────────────
// PBKDF2-HMAC-SHA256 via PKCS5_PBKDF2_HMAC (present in every OpenSSL 3, so this
// builds identically against the sandbox's libcrypto and Scott's static 3.5.7).
// The raw seal()/unseal() above are NOT touched: seal_with_passphrase derives a
// key and calls seal(), then records the KDF params on the returned envelope.
bytes derive_key(const std::string& passphrase, const bytes& salt, std::uint32_t iters) {
    if (salt.empty())
        throw std::runtime_error("folioedit: derive_key needs a non-empty salt");
    if (iters == 0 || iters > static_cast<std::uint32_t>(INT_MAX))
        throw std::runtime_error("folioedit: bad KDF iteration count");

    bytes key(static_cast<std::size_t>(KEY_LEN));
    if (PKCS5_PBKDF2_HMAC(passphrase.data(), to_int_len(passphrase.size(), "passphrase"),
                          salt.data(), to_int_len(salt.size(), "salt"),
                          static_cast<int>(iters), EVP_sha256(),
                          KEY_LEN, key.data()) != 1)
        throw std::runtime_error("folioedit: PBKDF2 derivation failed");
    return key;
}

Envelope seal_with_passphrase(const bytes& plaintext, const std::string& passphrase,
                              std::uint32_t iters) {
    bytes salt(static_cast<std::size_t>(SALT_LEN));
    if (RAND_bytes(salt.data(), SALT_LEN) != 1)
        throw std::runtime_error("folioedit: RAND_bytes (salt) failed");

    // Canonicalize at the seam: the SAME fold runs on open, so spacing/case/
    // quoting of the phrase can never change the derived key (Passphrase.hpp).
    const bytes key = derive_key(canonicalize_passphrase(passphrase), salt, iters);
    Envelope env    = seal(plaintext, key);   // fills nonce / ciphertext / tag
    env.kdf_id      = KdfId::Pbkdf2HmacSha256;
    env.kdf_iters   = iters;
    env.salt        = salt;
    return env;
}

bytes unseal_with_passphrase(const Envelope& env, const std::string& passphrase) {
    if (env.kdf_id != KdfId::Pbkdf2HmacSha256)
        throw std::runtime_error("folioedit: envelope carries no PBKDF2 KDF params");
    const bytes key = derive_key(canonicalize_passphrase(passphrase), env.salt, env.kdf_iters);
    return unseal(env, key);   // wrong passphrase -> wrong key -> tag fails -> throw
}

}  // namespace folioedit
