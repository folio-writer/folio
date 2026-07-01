// folioedit :: Seal tests -- AES-256-GCM round-trip, tamper + wrong-key
// rejection, nonce uniqueness, and a full-engine capstone (seal a real
// Document, unseal, parse, verify the custody chain). Needs libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_seal.cpp ../src/Seal_openssl.cpp ../src/Format.cpp ../src/Custody.cpp -lcrypto -o test_seal && ./test_seal
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include -I "$OSSL/include" TEST_seal.cpp ../src/Seal_openssl.cpp ../src/Format.cpp ../src/Custody.cpp "$OSSL/libcrypto.a" -ldl -lpthread -o test_seal && ./test_seal
*/

#include "folioedit/Seal.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Custody.hpp"

#include <cstdio>
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
static std::string to_string(const fe::bytes& b) {
    return std::string(b.begin(), b.end());
}
static fe::bytes make_key(std::uint8_t seed) {
    fe::bytes k(32);
    for (std::size_t i = 0; i < k.size(); ++i)
        k[i] = static_cast<std::uint8_t>(seed + i);
    return k;
}
// true iff unseal threw (i.e. the seal correctly rejected the input)
static bool rejected(const fe::Envelope& env, const fe::bytes& key) {
    try { fe::unseal(env, key); return false; }
    catch (const std::exception&) { return true; }
}

static void test_roundtrip() {
    fe::bytes key = make_key(0x10);
    fe::bytes pt  = to_bytes("The wind came down off the ridge like it had somewhere to be.");

    fe::Envelope env = fe::seal(pt, key);
    check("nonce is 12 bytes, tag is 16, ct==pt length",
          env.nonce.size() == 12 && env.tag.size() == 16 &&
          env.ciphertext.size() == pt.size());
    check("ciphertext differs from plaintext", env.ciphertext != pt);
    check("unseal recovers the plaintext", fe::unseal(env, key) == pt);

    // Empty plaintext still seals + verifies (edge case).
    fe::Envelope e0 = fe::seal({}, key);
    check("empty plaintext round-trips", fe::unseal(e0, key).empty());
}

static void test_rejections() {
    fe::bytes key = make_key(0x20);
    fe::bytes pt  = to_bytes("proposals, never spliced");
    fe::Envelope env = fe::seal(pt, key);

    check("wrong key is rejected", rejected(env, make_key(0x21)));

    { fe::Envelope e = env; e.ciphertext[0] ^= 0x01;
      check("flipped ciphertext byte is rejected", rejected(e, key)); }

    { fe::Envelope e = env; e.tag[0] ^= 0x01;
      check("flipped tag byte is rejected", rejected(e, key)); }

    // AAD binding: editing the framing header must fail the tag.
    { fe::Envelope e = env; e.schema = 2;
      check("tampered header (schema) is rejected", rejected(e, key)); }

    // Bad key size is rejected outright.
    { bool threw = false;
      try { fe::unseal(env, fe::bytes(16, 0)); } catch (const std::exception&) { threw = true; }
      check("wrong key size is rejected", threw); }
}

static void test_nonce_uniqueness() {
    fe::bytes key = make_key(0x30);
    fe::bytes pt  = to_bytes("same plaintext, same key, twice");
    fe::Envelope a = fe::seal(pt, key);
    fe::Envelope b = fe::seal(pt, key);
    check("two seals use different nonces", a.nonce != b.nonce);
    check("two seals produce different ciphertext", a.ciphertext != b.ciphertext);
    check("both still unseal to the same plaintext",
          fe::unseal(a, key) == pt && fe::unseal(b, key) == pt);
}

// Full engine: build a Document with a finalized custody chain, serialise, seal,
// unseal, parse, and verify the chain survived the whole trip intact.
static void test_full_engine_capstone() {
    fe::Document doc;
    doc.project_title = "Whispering Pines";
    doc.pass.source   = "jane";
    doc.scenes.push_back({"scn_k3f9", "The Ridge", 3, "<p>...</p>"});
    doc.annotations.push_back({"scn_k3f9", 33, 52, "watched the treeline",
                               "Proofreader", "filter verb"});

    fe::CustodyEvent issued;
    issued.seq = 0; issued.kind = fe::CustodyEvent_Kind::Issued;
    issued.actor = "scott"; issued.binds = fe::sha256_hex("body-v1");
    fe::finalize_event(issued, "");
    fe::CustodyEvent sealed;
    sealed.seq = 1; sealed.kind = fe::CustodyEvent_Kind::Sealed;
    sealed.actor = "jane"; sealed.binds = fe::sha256_hex("annotations-v1");
    fe::finalize_event(sealed, issued.hash);
    doc.custody = {issued, sealed};

    fe::bytes key = make_key(0x40);
    std::string json_out = fe::to_json(doc).dump();

    fe::Envelope env = fe::seal(to_bytes(json_out), key);
    std::string json_back = to_string(fe::unseal(env, key));
    fe::Document doc2 = fe::from_json(fe::json::parse(json_back));

    check("sealed JSON survives unseal byte-identical", json_out == json_back);
    check("custody chain verifies after seal round-trip", fe::verify_chain(doc2.custody));
    check("annotation survived the full trip",
          doc2.annotations.size() == 1 &&
          doc2.annotations[0].quote == "watched the treeline");
}

int main() {
    test_roundtrip();
    test_rejections();
    test_nonce_uniqueness();
    test_full_engine_capstone();
    std::printf("\nfolioedit seal: %d/%d\n", g_pass, g_total);
    std::printf("backend: %s\n", fe::seal_backend().c_str());
    return g_pass == g_total ? 0 : 1;
}
