// folioedit :: KDF tests -- PBKDF2-HMAC-SHA256 passphrase -> key, the
// passphrase seal/unseal layer over the raw seam, and a full-frame capstone
// (real Document -> JSON -> passphrase-seal -> envelope bytes -> back -> unseal
// -> parse -> verify_chain). Includes an RFC-style known-answer vector so a
// regression in the derivation is caught, not just "it round-trips." Needs
// libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_kdf.cpp ../src/Seal_openssl.cpp ../src/Format.cpp ../src/Custody.cpp -lcrypto -o test_kdf && ./test_kdf
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include -I "$OSSL/include" TEST_kdf.cpp ../src/Seal_openssl.cpp ../src/Format.cpp ../src/Custody.cpp "$OSSL/libcrypto.a" -ldl -lpthread -o test_kdf && ./test_kdf
*/

#include "folioedit/Seal.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Custody.hpp"

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

static fe::bytes to_bytes(const std::string& s) {
    return fe::bytes(s.begin(), s.end());
}
static std::string to_hex(const fe::bytes& b) {
    static const char* h = "0123456789abcdef";
    std::string out;
    out.reserve(b.size() * 2);
    for (std::uint8_t c : b) { out.push_back(h[(c >> 4) & 0xF]); out.push_back(h[c & 0xF]); }
    return out;
}
static bool threw(const std::function<void()>& f) {
    try { f(); return false; } catch (const std::exception&) { return true; }
}

// PBKDF2-HMAC-SHA256, password "password", salt "salt", c=1, dkLen=32 -- a
// widely published test vector. Proves derive_key computes standard PBKDF2, not
// a look-alike.
static void test_known_answer() {
    fe::bytes key = fe::derive_key("password", to_bytes("salt"), 1);
    check("PBKDF2-HMAC-SHA256 c=1 matches published vector",
          to_hex(key) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");

    fe::bytes key2 = fe::derive_key("password", to_bytes("salt"), 2);
    check("PBKDF2-HMAC-SHA256 c=2 matches published vector",
          to_hex(key2) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    check("derived key is 32 bytes", key.size() == 32);
}

static void test_derive_properties() {
    fe::bytes s1 = to_bytes("salt-one-16-byte");
    fe::bytes s2 = to_bytes("salt-two-16-byte");

    check("same pass+salt+iters is deterministic",
          fe::derive_key("hunter2", s1, 4096) == fe::derive_key("hunter2", s1, 4096));
    check("different salt -> different key",
          fe::derive_key("hunter2", s1, 4096) != fe::derive_key("hunter2", s2, 4096));
    check("different iters -> different key",
          fe::derive_key("hunter2", s1, 4096) != fe::derive_key("hunter2", s1, 5000));
    check("different passphrase -> different key",
          fe::derive_key("hunter2", s1, 4096) != fe::derive_key("hunter3", s1, 4096));

    check("empty salt is rejected", threw([&]{ fe::derive_key("x", {}, 4096); }));
    check("zero iterations is rejected", threw([&]{ fe::derive_key("x", s1, 0); }));
}

static void test_passphrase_seal() {
    fe::bytes pt = to_bytes("the author stays the authority; proposals, never spliced");

    fe::Envelope env = fe::seal_with_passphrase(pt, "correct horse battery staple", 4096);
    check("envelope records PBKDF2 kdf_id",   env.kdf_id == fe::KdfId::Pbkdf2HmacSha256);
    check("envelope records iteration count", env.kdf_iters == 4096);
    check("envelope carries a 16-byte salt",  env.salt.size() == 16);
    check("ciphertext differs from plaintext", env.ciphertext != pt);

    check("right passphrase recovers plaintext",
          fe::unseal_with_passphrase(env, "correct horse battery staple") == pt);
    check("wrong passphrase is rejected",
          threw([&]{ fe::unseal_with_passphrase(env, "Tr0ub4dor&3"); }));

    // Fresh random salt per seal -> same plaintext+passphrase seals differently.
    fe::Envelope env2 = fe::seal_with_passphrase(pt, "correct horse battery staple", 4096);
    check("second seal draws a fresh salt",       env2.salt != env.salt);
    check("second seal yields different ciphertext", env2.ciphertext != env.ciphertext);
    check("second seal still opens with the passphrase",
          fe::unseal_with_passphrase(env2, "correct horse battery staple") == pt);

    // A raw-key (None kdf) envelope has no passphrase params.
    fe::bytes raw_key(32, 0x7);
    fe::Envelope raw = fe::seal(pt, raw_key);
    check("None-kdf envelope rejects passphrase unseal",
          threw([&]{ fe::unseal_with_passphrase(raw, "anything"); }));

    // Default iteration count actually runs (sanity on the real work factor).
    fe::Envelope d = fe::seal_with_passphrase(to_bytes("x"), "pw");
    check("default-iters seal round-trips",
          d.kdf_iters == fe::KDF_DEFAULT_ITERS &&
          fe::unseal_with_passphrase(d, "pw") == to_bytes("x"));
}

// End to end: real Document, sealed by passphrase, pushed through the binary
// envelope frame and back, unsealed, parsed, custody chain re-verified. Proves
// the KDF params survive envelope_to_bytes/from_bytes and the whole path composes.
static void test_capstone() {
    fe::Document doc;
    doc.project_id    = "prj_kdf";
    doc.project_title = "The Passphrase Pass";
    doc.version_stamp = "v-2026-07-01";
    doc.pass.id       = "pass_1";
    doc.pass.source   = "jane";

    fe::Scene sc; sc.iid = "scn_a1"; sc.title = "Open"; sc.order = 0;
    sc.text = "<p>The wind came down off the ridge like it had somewhere to be.</p>";
    doc.scenes.push_back(sc);

    fe::Annotation an;
    an.scene_iid = "scn_a1"; an.range_start = 4; an.range_end = 8;
    an.quote = "wind"; an.kind = "Editor"; an.text = "Consider a colder verb.";
    doc.annotations.push_back(an);

    // Two-event custody chain (issued -> sealed), finalized.
    fe::CustodyEvent e0;
    e0.seq = 0; e0.kind = fe::CustodyEvent_Kind::Issued;
    e0.actor = "scott"; e0.actor_id = "fp_scott"; e0.at = "2026-07-01T00:00:00Z";
    e0.binds = fe::sha256_hex(sc.text);
    fe::finalize_event(e0, "");
    doc.custody.push_back(e0);

    fe::CustodyEvent e1;
    e1.seq = 1; e1.kind = fe::CustodyEvent_Kind::Sealed;
    e1.actor = "jane"; e1.actor_id = "fp_jane"; e1.at = "2026-07-02T00:00:00Z";
    e1.binds = fe::sha256_hex(an.text);
    fe::finalize_event(e1, e0.hash);
    doc.custody.push_back(e1);

    check("chain verifies before sealing", fe::verify_chain(doc.custody));

    const std::string plaintext = fe::to_json(doc).dump();
    fe::Envelope env = fe::seal_with_passphrase(to_bytes(plaintext), "open sesame", 4096);

    // Through the on-disk frame and back.
    fe::bytes    raw  = fe::envelope_to_bytes(env);
    fe::Envelope back = fe::envelope_from_bytes(raw);
    check("kdf params survive the binary frame",
          back.kdf_id == fe::KdfId::Pbkdf2HmacSha256 &&
          back.kdf_iters == env.kdf_iters && back.salt == env.salt);

    fe::bytes    opened = fe::unseal_with_passphrase(back, "open sesame");
    fe::Document doc2   = fe::from_json(fe::json::parse(std::string(opened.begin(), opened.end())));
    check("custody chain still verifies after the full round-trip",
          fe::verify_chain(doc2.custody));
    check("an annotation survived intact",
          doc2.annotations.size() == 1 && doc2.annotations[0].quote == "wind");
}

int main() {
    std::printf("folioedit KDF tests\n");
    test_known_answer();
    test_derive_properties();
    test_passphrase_seal();
    test_capstone();
    std::printf("\nfolioedit kdf: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
