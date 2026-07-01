// folioedit :: Identity tests -- Ed25519 keypair generation, the public-key
// fingerprint (actor_id), sign/verify over an event hash, keyfile round-trip,
// and the property that gives signatures their teeth: an attacker who edits a
// bound field can repair the hash chain (verify_chain passes) but cannot re-sign
// without the private key (verify_event_signature fails). Needs libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_identity.cpp ../src/Identity_openssl.cpp ../src/Custody.cpp ../src/KeyHex.cpp ../src/Format.cpp -lcrypto -o test_identity && ./test_identity
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include -I "$OSSL/include" TEST_identity.cpp ../src/Identity_openssl.cpp ../src/Custody.cpp ../src/KeyHex.cpp ../src/Format.cpp "$OSSL/libcrypto.a" -ldl -lpthread -o test_identity && ./test_identity
*/

#include "folioedit/Identity.hpp"
#include "folioedit/Custody.hpp"
#include "folioedit/Format.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}

static fe::CustodyEvent make_issued(const fe::KeyPair& kp) {
    fe::CustodyEvent e;
    e.seq = 0; e.kind = fe::CustodyEvent_Kind::Issued;
    e.actor = "jane"; e.actor_id = fe::fingerprint(kp.public_key);
    e.at = "2026-07-01T00:00:00Z";
    e.binds = fe::sha256_hex("the scenes that went out");
    fe::finalize_event(e, "");   // hash commits to actor_id (identity) too
    return e;
}

static void test_keys_and_fingerprint() {
    fe::KeyPair a = fe::generate_keypair();
    fe::KeyPair b = fe::generate_keypair();
    check("private key is 32 bytes", a.private_key.size() == 32);
    check("public key is 32 bytes",  a.public_key.size() == 32);
    check("two keypairs differ",     a.private_key != b.private_key);

    std::string fp = fe::fingerprint(a.public_key);
    check("fingerprint is 64 hex chars (sha256)", fp.size() == 64);
    check("fingerprint is deterministic", fp == fe::fingerprint(a.public_key));
    check("different keys -> different fingerprints",
          fp != fe::fingerprint(b.public_key));
}

static void test_sign_verify() {
    fe::KeyPair kp    = fe::generate_keypair();
    fe::KeyPair other = fe::generate_keypair();
    const std::string h = fe::sha256_hex("event contents");

    std::string sig = fe::sign_hash(kp.private_key, h);
    check("signature is 128 hex chars (64-byte ed25519)", sig.size() == 128);
    check("valid signature verifies", fe::verify_hash(kp.public_key, h, sig));

    check("wrong public key rejects", !fe::verify_hash(other.public_key, h, sig));
    check("tampered hash rejects",
          !fe::verify_hash(kp.public_key, fe::sha256_hex("other contents"), sig));

    std::string bad = sig; bad[0] = (bad[0] == 'a' ? 'b' : 'a');
    check("tampered signature rejects", !fe::verify_hash(kp.public_key, h, bad));
    check("malformed signature hex returns false, not throw",
          !fe::verify_hash(kp.public_key, h, "nothex"));
    check("empty signature returns false", !fe::verify_hash(kp.public_key, h, ""));

    // Signatures are randomised nonce-free (deterministic ed25519): same inputs
    // give the same signature, and it still verifies.
    check("ed25519 is deterministic", fe::sign_hash(kp.private_key, h) == sig);
}

static void test_event_level() {
    fe::KeyPair kp = fe::generate_keypair();
    fe::CustodyEvent e = make_issued(kp);

    check("actor_id was bound as the fingerprint before signing",
          e.actor_id == fe::fingerprint(kp.public_key));
    fe::sign_event(e, kp);
    check("sign_event populates signature", !e.signature.empty());
    check("event signature verifies", fe::verify_event_signature(e, kp.public_key));

    fe::KeyPair other = fe::generate_keypair();
    check("event signature rejects a different key",
          !fe::verify_event_signature(e, other.public_key));

    // Signing with a key whose fingerprint != the event's actor_id is a mistake.
    fe::CustodyEvent e2 = make_issued(kp);
    bool mismatch_threw = false;
    try { fe::sign_event(e2, other); } catch (const std::exception&) { mismatch_threw = true; }
    check("signing with a mismatched identity throws", mismatch_threw);

    fe::CustodyEvent unsigned_e = make_issued(kp);
    check("unsigned event verifies false",
          !fe::verify_event_signature(unsigned_e, kp.public_key));

    // sign_event on an unfinalized (empty-hash) event throws.
    bool threw = false;
    try { fe::CustodyEvent blank; fe::sign_event(blank, kp); }
    catch (const std::exception&) { threw = true; }
    check("signing an unfinalized event throws", threw);
}

// The teeth: integrity alone can be repaired by anyone; the signature can't.
static void test_repair_still_fails() {
    fe::KeyPair editor = fe::generate_keypair();

    // A real signed two-event chain.
    fe::CustodyEvent e0 = make_issued(editor);
    fe::sign_event(e0, editor);

    fe::CustodyEvent e1;
    e1.seq = 1; e1.kind = fe::CustodyEvent_Kind::Sealed;
    e1.actor = "jane"; e1.actor_id = fe::fingerprint(editor.public_key);
    e1.at = "2026-07-02T00:00:00Z";
    e1.binds = fe::sha256_hex("the annotations coming back");
    fe::finalize_event(e1, e0.hash);
    fe::sign_event(e1, editor);

    std::vector<fe::CustodyEvent> chain = {e0, e1};
    check("signed chain verifies (integrity)", fe::verify_chain(chain));
    check("both events' signatures verify",
          fe::verify_event_signature(chain[0], editor.public_key) &&
          fe::verify_event_signature(chain[1], editor.public_key));

    // Attacker (no private key) edits a bound field on e1 and REPAIRS the chain:
    // recompute e1.hash and re-link. verify_chain now passes again...
    chain[1].binds = fe::sha256_hex("a forged, friendlier note");
    fe::finalize_event(chain[1], chain[0].hash);
    check("attacker can repair the hash chain (integrity passes)",
          fe::verify_chain(chain));

    // ...but the signature was over the OLD hash and they can't re-sign.
    check("but the signature no longer verifies (forgery caught)",
          !fe::verify_event_signature(chain[1], editor.public_key));
}

static void test_persistence() {
    fe::KeyPair kp = fe::generate_keypair();
    std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp")
                       + "/folioedit_test_identity.key";

    fe::save_keypair(kp, path);
    fe::KeyPair loaded = fe::load_keypair(path);
    check("loaded private key matches", loaded.private_key == kp.private_key);
    check("loaded public key matches",  loaded.public_key == kp.public_key);

    // A signature made with the loaded key verifies against the original public key.
    const std::string h = fe::sha256_hex("persisted-key signing");
    check("signature from loaded key verifies against original pub",
          fe::verify_hash(kp.public_key, h, fe::sign_hash(loaded.private_key, h)));

    std::remove(path.c_str());

    bool threw = false;
    try { fe::load_keypair(path); } catch (const std::exception&) { threw = true; }
    check("loading a missing key file throws", threw);
}

// A signed event must survive the JSON round-trip (into the sealed body and back)
// with its signature still verifiable -- otherwise the "who" is lost on import.
static void test_json_roundtrip() {
    fe::KeyPair editor = fe::generate_keypair();
    fe::CustodyEvent e = make_issued(editor);
    fe::sign_event(e, editor);

    fe::Document doc;
    doc.project_id = "prj_id";
    doc.custody.push_back(e);

    fe::Document back = fe::from_json(fe::to_json(doc));
    check("custody survived JSON round-trip", back.custody.size() == 1);
    check("signature + actor_id survived JSON",
          back.custody[0].signature == e.signature &&
          back.custody[0].actor_id == e.actor_id);
    check("signature still verifies after JSON round-trip",
          fe::verify_event_signature(back.custody[0], editor.public_key) &&
          fe::verify_chain(back.custody));
}

int main() {
    std::printf("folioedit Identity tests\n");
    test_keys_and_fingerprint();
    test_sign_verify();
    test_event_level();
    test_repair_still_fails();
    test_persistence();
    test_json_roundtrip();
    std::printf("\nfolioedit identity: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
