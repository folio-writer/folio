// folioedit :: pure SHA-256 equivalence gate (s18.4).
//
// This is the keystone check for the two-faces cutover: it proves the bundled
// pure-STL SHA-256 (Sha256.cpp) is byte-identical to the libcrypto SHA it
// replaces, so the switch is invisible to every signed, chained, on-disk custody
// artifact. Three layers of evidence:
//   1. FIPS 180-4 known-answer vectors (empty, "abc", the 448-bit message, and
//      the one-million-'a' message) -- the standard's own gate.
//   2. An equivalence battery: the exact `binds` input strings the green s102
//      custody/seal/identity/timestamp/archive fixtures hash, each with its
//      expected digest computed against libcrypto and pinned here.
//   3. The full make_chain() fixture (issued -> sealed -> imported, identical to
//      TEST_custody's) with each event's stored hash pinned to the value the
//      libcrypto build produced -- so the whole length-prefixed canonical form +
//      chain linkage reproduces bit-for-bit.
// If all three pass, the pure impl computes the same custody hashes as before and
// libcrypto's SHA can be removed with no on-disk change. Pins are the libcrypto-
// computed truth; a drift here means the pure impl (or the canonical form) moved.
//
// Links ONLY the pure TUs -- NO libcrypto -- which is the point: the hash layer
// now stands on its own.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_sha256.cpp ../src/Sha256.cpp ../src/Custody.cpp -o test_sha256 && ./test_sha256
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_sha256.cpp ../src/Sha256.cpp ../src/Custody.cpp -o test_sha256 && ./test_sha256
*/

#include "folioedit/Custody.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}

// ── 1. FIPS 180-4 known-answer vectors ───────────────────────────────────────
static void test_fips_vectors() {
    check("SHA-256(\"\")",
          fe::sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("SHA-256(\"abc\")",
          fe::sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    check("SHA-256(448-bit message)",
          fe::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    check("SHA-256(one million 'a')",
          fe::sha256_hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// ── 2. Equivalence battery: the exact fixture inputs (libcrypto-pinned) ──────
static void test_fixture_equivalence() {
    struct KA { const char* in; const char* out; };
    static const KA cases[] = {
        { "body-v1"                     , "515d48841eb6e21b76d51689cabb8f2d055c40e69af988cd1cb453f1bfcb6ec2" },
        { "body-v2"                     , "7f3a0791bb4a438338ed1c19ad5ff4a207943768679857a31163c4a1a21eb0cb" },
        { "annotations-v1"              , "dcc6c29aa220972b8645c8a5bcb510895827a597e20662bb5dead79962c2d691" },
        { "body"                        , "230d8358dc8e8890b4c58deeb62912ee2f20357ae92a5cc861b98e68fe31acb5" },
        { "annotations"                 , "295df243c6a33994c30b6e16aea7ce6155a24b44514ec956d7a09ae0a4cb0411" },
        { "the scenes that went out"    , "961385fbd400a921fda7fea7fe67a655ddda744fff7e54a41110ccb6d767de9f" },
        { "event contents"              , "3ce3284d8928d57c416a48d71caa3c5d2f274a2ac1e76ec02f43043021fee8c2" },
        { "other contents"              , "425ecb5e1000c9545b105f1547c6d6f03d1cd4b38d94d457ec032907e8b51079" },
        { "the annotations coming back" , "c95f4b75e267d21f9e83325bb69bd69505bd46f07b6804b54606d9c82cda8bb3" },
        { "a forged, friendlier note"   , "66a12e404e0b53ad4827f5f0c0ca990e20a7e41e903a25e661bb1a9328409539" },
        { "persisted-key signing"       , "b182d8d2d81b5b09a4de982f148020725dc1e4352067819726004437bdeca4e0" },
        { "the event to be stamped"     , "f11febd5353f8b3c3bfcc17e84f7a94d98e4515a09d48beff20d515c9853bbae" },
        { "stamp me at a trusted time"  , "3aeeacbfd76253cca44e0a2eabfcf847fc55ae340d27779d0c8a89954446fb60" },
        { "a different event"           , "5a0eab9c90a9f953825a38fabcca884db5d80a30ee076742ba7736d2839590c9" },
        { "annotations coming back"     , "257ddf42ab899ebfa0525796e7440ab2aa5cb38c5bf0a4117c605bf3ac697e93" },
        { "scenes out"                  , "3723f96aecb0e21f8a33dad2f61bb0b02fe22feb90ae719317a71fe18928333a" },
    };
    bool all = true;
    for (const KA& c : cases)
        if (fe::sha256_hex(c.in) != c.out) all = false;
    check("all s102 fixture `binds` inputs match libcrypto", all);
}

// ── 3. Full custody-chain equivalence (pinned to the libcrypto build) ────────
// The exact make_chain() fixture from TEST_custody. Its three event hashes are
// pinned to the values the previous libcrypto build produced, so this proves the
// pure SHA reproduces the whole canonical-form + chain arithmetic byte for byte.
static std::vector<fe::CustodyEvent> make_chain() {
    std::vector<fe::CustodyEvent> chain;

    fe::CustodyEvent issued;
    issued.seq = 0; issued.kind = fe::CustodyEvent_Kind::Issued;
    issued.actor = "scott"; issued.actor_id = "ed25519:aa11";
    issued.at = "2026-07-01T10:00:00Z"; issued.binds = fe::sha256_hex("body-v1");
    issued.time_source = fe::TimeSource::Rfc3161; issued.timestamp_token = "tok0";
    fe::finalize_event(issued, "");
    chain.push_back(issued);

    fe::CustodyEvent sealed;
    sealed.seq = 1; sealed.kind = fe::CustodyEvent_Kind::Sealed;
    sealed.actor = "jane"; sealed.actor_id = "ed25519:bb22";
    sealed.at = "2026-07-01T14:00:00Z"; sealed.binds = fe::sha256_hex("annotations-v1");
    sealed.time_source = fe::TimeSource::LocalClock;
    fe::finalize_event(sealed, chain.back().hash);
    chain.push_back(sealed);

    fe::CustodyEvent imported;
    imported.seq = 2; imported.kind = fe::CustodyEvent_Kind::Imported;
    imported.actor = "scott"; imported.actor_id = "ed25519:aa11";
    imported.at = "2026-07-01T16:00:00Z"; imported.binds = fe::sha256_hex("body-v1");
    imported.time_source = fe::TimeSource::LocalClock;
    fe::finalize_event(imported, chain.back().hash);
    chain.push_back(imported);

    return chain;
}

static void test_chain_hashes_pinned() {
    auto chain = make_chain();
    check("chain verifies", fe::verify_chain(chain));
    check("event #0 (issued) hash matches libcrypto build",
          chain[0].hash == "e4fd1180d54e6a4d18a211afb94451bd7bb1a82071efb989c2c05a95fc6973dc");
    check("event #1 (sealed) hash matches libcrypto build",
          chain[1].hash == "015f50eacc717e5939e6dda816687fb70c7edefa6fdfba3527241d94162cf60b");
    check("event #2 (imported) hash matches libcrypto build",
          chain[2].hash == "eee0c6ec50f24d2f7cd6623ba3c0e0a22c539d65417239e163a9bb67f6709db9");
}

int main() {
    test_fips_vectors();
    test_fixture_equivalence();
    test_chain_hashes_pinned();
    std::printf("\nfolioedit pure SHA-256 equivalence: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
