// folioedit :: Custody tests -- SHA-256 known-answer vectors, a real
// issued->sealed->imported chain, and tamper detection on every bound field.
// Needs libcrypto (SHA-256).
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_custody.cpp ../src/Custody.cpp -lcrypto -o test_custody && ./test_custody
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_custody.cpp ../src/Custody.cpp -I "$OSSL/include" "$OSSL/libcrypto.a" -ldl -lpthread -o test_custody && ./test_custody
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

// Build a valid 3-event chain: issued -> sealed -> imported, each finalized so
// prev_hash + hash link correctly.
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

static void test_sha_known_answers() {
    // NIST/FIPS-180 known vectors.
    check("SHA-256(\"\") known answer",
          fe::sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    check("SHA-256(\"abc\") known answer",
          fe::sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_chain_valid() {
    auto chain = make_chain();
    check("issued->sealed->imported verifies", fe::verify_chain(chain));
    check("chain links (prev_hash == prior hash)",
          chain[1].prev_hash == chain[0].hash &&
          chain[2].prev_hash == chain[1].hash &&
          chain[0].prev_hash.empty());
}

static void test_tamper_detection() {
    // Flip a bound field WITHOUT recomputing hash -> chain must break.
    {
        auto chain = make_chain();
        chain[1].actor = "mallory";                 // bound, hash not updated
        check("tampered actor breaks chain", !fe::verify_chain(chain));
    }
    {
        auto chain = make_chain();
        chain[0].binds = fe::sha256_hex("body-v2");  // back-swap the sent version
        check("tampered binds breaks chain", !fe::verify_chain(chain));
    }
    {
        auto chain = make_chain();
        chain[1].at = "2020-01-01T00:00:00Z";        // back-date
        check("tampered timestamp breaks chain", !fe::verify_chain(chain));
    }
    {
        auto chain = make_chain();
        chain[2].hash = chain[0].hash;               // forge a stored hash
        check("forged hash breaks chain", !fe::verify_chain(chain));
    }
    {
        auto chain = make_chain();
        std::swap(chain[1], chain[2]);               // reorder -> seq + linkage wrong
        check("reordered events break chain", !fe::verify_chain(chain));
    }
    {
        auto chain = make_chain();
        chain.erase(chain.begin() + 1);              // strip the sealed event
        check("stripped event breaks chain", !fe::verify_chain(chain));
    }
    {
        // Re-finalizing after a legitimate edit restores validity (proves the
        // break above is the missing recompute, not a false positive).
        auto chain = make_chain();
        chain[1].actor = "mallory";
        fe::finalize_event(chain[1], chain[0].hash);
        fe::finalize_event(chain[2], chain[1].hash); // relink downstream
        check("re-finalized chain verifies again", fe::verify_chain(chain));
    }
}

int main() {
    test_sha_known_answers();
    test_chain_valid();
    test_tamper_detection();
    std::printf("\nfolioedit custody: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
