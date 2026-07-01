// folioedit :: Format tests -- Document JSON round-trip (byte-stable) and the
// binary Envelope frame (round-trip + truncation/bad-magic rejection). Pure --
// no libcrypto needed.
//
// Build+run (bare, copy-paste as a block). Format now shares Custody's enum
// helpers, so link Custody.cpp + libcrypto too:
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_format.cpp ../src/Format.cpp ../src/Custody.cpp -lcrypto -o test_format && ./test_format
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_format.cpp ../src/Format.cpp ../src/Custody.cpp -I "$OSSL/include" "$OSSL/libcrypto.a" -ldl -lpthread -o test_format && ./test_format
*/

#include "folioedit/Format.hpp"
#include "folioedit/Envelope.hpp"

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

static fe::Document make_full_document() {
    fe::Document d;
    d.project_id    = "prj_wp";
    d.project_title = "Whispering Pines";
    d.version_stamp = "v_2026-07-01T00:00:00Z";

    d.pass.id     = "pass_001";
    d.pass.source = "jane";
    d.pass.kinds  = {"Proofreader", "Editor"};
    d.pass.rules  = "surface facts, never verdicts";

    d.scenes.push_back({"scn_k3f9", "The Ridge", 3, "<p>The wind came down...</p>"});
    d.scenes.push_back({"scn_a1b2", "Nightfall", 4, "<p>The cold found the seams...</p>"});

    d.annotations.push_back({"scn_k3f9", 33, 52, "watched the treeline",
                             "Proofreader", "filter verb -- 4th sight-verb in scene"});

    fe::CustodyEvent e0;
    e0.seq = 0; e0.kind = fe::CustodyEvent_Kind::Issued;
    e0.actor = "scott"; e0.actor_id = "ed25519:aa11"; e0.at = "2026-07-01T10:00:00Z";
    e0.binds = "sha256:body-abc"; e0.prev_hash = ""; e0.hash = "sha256:h0";
    e0.signature = "sig0"; e0.time_source = fe::TimeSource::Rfc3161;
    e0.timestamp_token = "tok0";

    fe::CustodyEvent e1;
    e1.seq = 1; e1.kind = fe::CustodyEvent_Kind::Sealed;
    e1.actor = "jane"; e1.actor_id = "ed25519:bb22"; e1.at = "2026-07-01T14:00:00Z";
    e1.binds = "sha256:ann-def"; e1.prev_hash = "sha256:h0"; e1.hash = "sha256:h1";
    e1.signature = "sig1"; e1.time_source = fe::TimeSource::LocalClock;

    d.custody.push_back(e0);
    d.custody.push_back(e1);
    return d;
}

static void test_document_roundtrip() {
    // Full document: dump -> parse -> dump must be byte-identical.
    fe::Document d = make_full_document();
    std::string s1 = fe::to_json(d).dump(2);
    fe::Document d2 = fe::from_json(fe::to_json(d));
    std::string s2 = fe::to_json(d2).dump(2);
    check("full document round-trips byte-identical", s1 == s2);

    // Spot-check reconstruction, not just self-consistency.
    check("scenes survive round-trip",
          d2.scenes.size() == 2 && d2.scenes[0].iid == "scn_k3f9" &&
          d2.scenes[0].order == 3 && d2.scenes[1].title == "Nightfall");
    check("pass survives round-trip",
          d2.pass.source == "jane" && d2.pass.kinds.size() == 2 &&
          d2.pass.kinds[1] == "Editor" && d2.pass.rules == d.pass.rules);
    check("annotation range + quote survive",
          d2.annotations.size() == 1 && d2.annotations[0].range_start == 33 &&
          d2.annotations[0].range_end == 52 &&
          d2.annotations[0].quote == "watched the treeline");
    check("custody chain survives (kind + time_source enums)",
          d2.custody.size() == 2 &&
          d2.custody[0].kind == fe::CustodyEvent_Kind::Issued &&
          d2.custody[0].time_source == fe::TimeSource::Rfc3161 &&
          d2.custody[1].kind == fe::CustodyEvent_Kind::Sealed &&
          d2.custody[1].time_source == fe::TimeSource::LocalClock &&
          d2.custody[1].prev_hash == "sha256:h0");

    // Minimal document (all-empty containers) also round-trips stably.
    fe::Document empty;
    std::string e1 = fe::to_json(empty).dump(2);
    std::string e2 = fe::to_json(fe::from_json(fe::to_json(empty))).dump(2);
    check("empty document round-trips byte-identical", e1 == e2);
}

static void test_envelope_frame() {
    fe::Envelope env;
    env.schema = 1;
    env.cipher = fe::CipherId::AesGcm256;
    env.salt       = {1, 2, 3, 4, 5, 6, 7, 8};
    env.nonce      = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};   // 12
    env.ciphertext = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22};
    env.tag        = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                      0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF}; // 16

    fe::bytes raw = fe::envelope_to_bytes(env);
    fe::Envelope back = fe::envelope_from_bytes(raw);

    check("envelope re-serialises byte-identical", raw == fe::envelope_to_bytes(back));
    check("envelope fields survive frame",
          back.schema == 1 && back.cipher == fe::CipherId::AesGcm256 &&
          back.salt == env.salt && back.nonce == env.nonce &&
          back.ciphertext == env.ciphertext && back.tag == env.tag);

    // Bad magic rejected.
    bool threw_magic = false;
    fe::bytes junk = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0};
    try { fe::envelope_from_bytes(junk); } catch (const std::exception&) { threw_magic = true; }
    check("bad magic is rejected", threw_magic);

    // Truncation rejected (chop the tail so a length prefix overruns).
    bool threw_trunc = false;
    fe::bytes chopped(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(raw.size() - 5));
    try { fe::envelope_from_bytes(chopped); } catch (const std::exception&) { threw_trunc = true; }
    check("truncated envelope is rejected", threw_trunc);
}

int main() {
    test_document_roundtrip();
    test_envelope_frame();
    std::printf("\nfolioedit format: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
