// folioedit :: Archive tests -- whole-file open/save (raw key + passphrase) with
// wrong-key / tamper-on-disk rejection, the kdf_id dispatch hint, the `binds`
// content hashes, append_event chain linkage, and a full lifecycle capstone
// (build -> issued+sealed events appended/signed -> save -> open -> verify).
// Needs libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_archive.cpp ../src/Archive.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Sha256.cpp ../src/Seal_openssl.cpp ../src/KeyHex.cpp ../src/Identity_openssl.cpp -lcrypto -o test_archive && ./test_archive
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include -I "$OSSL/include" TEST_archive.cpp ../src/Archive.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Sha256.cpp ../src/Seal_openssl.cpp ../src/KeyHex.cpp ../src/Identity_openssl.cpp "$OSSL/libcrypto.a" -ldl -lpthread -o test_archive && ./test_archive
*/

#include "folioedit/Archive.hpp"
#include "folioedit/Identity.hpp"
#include "folioedit/Custody.hpp"

#include <cstdio>
#include <cstdlib>
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
static bool threw(const std::function<void()>& f) {
    try { f(); return false; } catch (const std::exception&) { return true; }
}
static std::string tmp(const char* name) {
    const char* d = std::getenv("TMPDIR");
    return std::string(d ? d : "/tmp") + "/" + name;
}
static fe::bytes make_key(std::uint8_t seed) {
    fe::bytes k(32);
    for (std::size_t i = 0; i < k.size(); ++i) k[i] = static_cast<std::uint8_t>(seed + i);
    return k;
}

static fe::Document sample() {
    fe::Document d;
    d.project_id = "prj_arc"; d.project_title = "The Archive Pass";
    d.version_stamp = "v1";
    fe::Scene s; s.iid = "scn_a1"; s.title = "Open"; s.order = 0;
    s.text = "<p>The wind came down off the ridge.</p>";
    d.scenes.push_back(s);
    fe::Annotation a; a.scene_iid = "scn_a1"; a.range_start = 4; a.range_end = 8;
    a.quote = "wind"; a.kind = "Editor"; a.text = "Colder verb?";
    d.annotations.push_back(a);
    return d;
}

static void test_raw_key_file() {
    const std::string path = tmp("folioedit_arc_raw.folioedit");
    fe::Document doc = sample();
    fe::bytes key = make_key(0x30);

    fe::save_document(path, doc, key);
    fe::Document back = fe::open_document(path, key);
    check("raw-key round-trip preserves the document",
          back.project_id == doc.project_id &&
          back.scenes.size() == 1 && back.annotations.size() == 1 &&
          back.annotations[0].quote == "wind");

    check("kdf_id on disk is None for a raw-key file",
          fe::read_envelope_file(path).kdf_id == fe::KdfId::None);
    check("wrong raw key is rejected",
          threw([&]{ fe::open_document(path, make_key(0x31)); }));

    std::remove(path.c_str());
}

static void test_passphrase_file() {
    const std::string path = tmp("folioedit_arc_pw.folioedit");
    fe::Document doc = sample();

    fe::save_document_pw(path, doc, "correct horse battery staple");
    fe::Document back = fe::open_document_pw(path, "correct horse battery staple");
    check("passphrase round-trip preserves the document",
          back.annotations.size() == 1 && back.annotations[0].text == "Colder verb?");
    check("withdrawn defaults false through the round-trip",
          back.annotations[0].withdrawn == false);

    fe::Envelope env = fe::read_envelope_file(path);
    check("kdf_id on disk is PBKDF2 for a passphrase file",
          env.kdf_id == fe::KdfId::Pbkdf2HmacSha256 && env.salt.size() == 16);
    check("wrong passphrase is rejected",
          threw([&]{ fe::open_document_pw(path, "Tr0ub4dor&3"); }));

    // Tamper a byte in the sealed file on disk -> open must fail (GCM tag).
    {
        std::FILE* f = std::fopen(path.c_str(), "rb+");
        std::fseek(f, -3, SEEK_END);
        int c = std::fgetc(f);
        std::fseek(f, -1, SEEK_CUR);
        std::fputc(c ^ 0x01, f);
        std::fclose(f);
    }
    check("a tampered file on disk is rejected",
          threw([&]{ fe::open_document_pw(path, "correct horse battery staple"); }));

    std::remove(path.c_str());
}

static void test_binds_hashes() {
    fe::Document a = sample();
    check("body_hash is deterministic",      fe::body_hash(a) == fe::body_hash(a));
    check("annotations_hash is deterministic", fe::annotations_hash(a) == fe::annotations_hash(a));
    check("body_hash is 64 hex chars",       fe::body_hash(a).size() == 64);

    fe::Document b = a; b.scenes[0].text += " More.";
    check("changing scene text changes body_hash", fe::body_hash(a) != fe::body_hash(b));
    check("but not annotations_hash", fe::annotations_hash(a) == fe::annotations_hash(b));

    fe::Document c = a; c.annotations[0].text = "different note";
    check("changing an annotation changes annotations_hash",
          fe::annotations_hash(a) != fe::annotations_hash(c));

    // A `del` tombstone (withdrawn) must change the hash -- so a sealed pass
    // commits that the annotation existed and was withdrawn (a court trace).
    fe::Document w = a; w.annotations[0].withdrawn = true;
    check("withdrawing an annotation changes annotations_hash",
          fe::annotations_hash(a) != fe::annotations_hash(w));
    check("the withdrawn annotation is preserved, not deleted",
          w.annotations.size() == a.annotations.size());

    // s107 -- a recorded verdict binds into the hash the way `withdrawn` does, so
    // a sealed return commits the author's accept/decline (court-visible history).
    fe::Document acc = a; acc.annotations[0].verdict = fe::Verdict::Accepted;
    fe::Document dec = a; dec.annotations[0].verdict = fe::Verdict::Declined;
    check("accepting an annotation changes annotations_hash",
          fe::annotations_hash(a) != fe::annotations_hash(acc));
    check("declining an annotation changes annotations_hash",
          fe::annotations_hash(a) != fe::annotations_hash(dec));
    check("accept vs decline are distinct in the hash",
          fe::annotations_hash(acc) != fe::annotations_hash(dec));
    // The default (proposed) is omitted from the canonical form, so an all-proposed
    // block hashes byte-identically to the pre-s107 shape -- no version bump needed,
    // old sealed files still verify. (Setting a verdict back to Proposed == unset.)
    fe::Document prop = acc; prop.annotations[0].verdict = fe::Verdict::Proposed;
    check("proposed verdict does not perturb the hash (omit-when-default)",
          fe::annotations_hash(a) == fe::annotations_hash(prop));
}

static void test_append_event() {
    fe::KeyPair editor = fe::generate_keypair();
    std::vector<fe::CustodyEvent> chain;

    fe::CustodyEvent issued;
    issued.kind = fe::CustodyEvent_Kind::Issued;
    issued.actor = "scott"; issued.actor_id = fe::fingerprint(editor.public_key);
    issued.at = "2026-07-01T00:00:00Z"; issued.binds = fe::sha256_hex("body");
    fe::CustodyEvent& e0 = fe::append_event(chain, issued);
    check("first event gets seq 0 and empty prev_hash",
          e0.seq == 0 && e0.prev_hash.empty() && !e0.hash.empty());
    fe::sign_event(e0, editor);

    fe::CustodyEvent sealed;
    sealed.kind = fe::CustodyEvent_Kind::Sealed;
    sealed.actor = "jane"; sealed.actor_id = fe::fingerprint(editor.public_key);
    sealed.at = "2026-07-02T00:00:00Z"; sealed.binds = fe::sha256_hex("annotations");
    fe::CustodyEvent& e1 = fe::append_event(chain, sealed);
    check("second event links seq 1 + prev_hash to the first",
          e1.seq == 1 && e1.prev_hash == chain[0].hash);
    fe::sign_event(e1, editor);

    check("appended chain verifies", fe::verify_chain(chain));
    check("both appended events' signatures verify",
          fe::verify_event_signature(chain[0], editor.public_key) &&
          fe::verify_event_signature(chain[1], editor.public_key));
}

// Full lifecycle: build a doc, record issued+sealed custody bound to the real
// content hashes, sign both, save under a passphrase, reopen, re-verify.
static void test_capstone() {
    const std::string path = tmp("folioedit_arc_cap.folioedit");
    fe::KeyPair editor = fe::generate_keypair();
    fe::Document doc = sample();
    const std::string fp = fe::fingerprint(editor.public_key);

    fe::CustodyEvent issued;
    issued.kind = fe::CustodyEvent_Kind::Issued; issued.actor = "scott";
    issued.actor_id = fp; issued.at = "2026-07-01T00:00:00Z";
    issued.binds = fe::body_hash(doc);
    fe::sign_event(fe::append_event(doc.custody, issued), editor);

    fe::CustodyEvent sealed;
    sealed.kind = fe::CustodyEvent_Kind::Sealed; sealed.actor = "jane";
    sealed.actor_id = fp; sealed.at = "2026-07-02T00:00:00Z";
    sealed.binds = fe::annotations_hash(doc);
    fe::sign_event(fe::append_event(doc.custody, sealed), editor);

    fe::save_document_pw(path, doc, "open sesame");
    fe::Document back = fe::open_document_pw(path, "open sesame");

    check("capstone: chain verifies after file round-trip", fe::verify_chain(back.custody));
    check("capstone: both signatures verify after round-trip",
          back.custody.size() == 2 &&
          fe::verify_event_signature(back.custody[0], editor.public_key) &&
          fe::verify_event_signature(back.custody[1], editor.public_key));
    check("capstone: issued binds still matches recomputed body_hash",
          back.custody[0].binds == fe::body_hash(back));
    check("capstone: sealed binds still matches recomputed annotations_hash",
          back.custody[1].binds == fe::annotations_hash(back));

    std::remove(path.c_str());
}

// s107 — the continuous RETURN: from a signed+sealed carrier, build the author's
// return (verdicts folded in, whole prior chain preserved, a new author `sealed`
// event appended), sign that event, seal, reopen, and verify the full 3-link
// chain + all signatures + that the return binds the verdict-laden hash.
static void test_make_return() {
    const std::string path = tmp("folioedit_arc_ret.folioedit");
    fe::KeyPair editor = fe::generate_keypair();
    fe::KeyPair author = fe::generate_keypair();
    const std::string efp = fe::fingerprint(editor.public_key);
    const std::string afp = fe::fingerprint(author.public_key);

    // A carrier as the author received + absorbed it: issued(author) + sealed(editor).
    fe::Document sent = sample();
    fe::CustodyEvent issued;
    issued.kind = fe::CustodyEvent_Kind::Issued; issued.actor = "scott";
    issued.actor_id = afp; issued.at = "2026-07-01T00:00:00Z";
    issued.binds = fe::body_hash(sent);
    fe::sign_event(fe::append_event(sent.custody, issued), author);
    fe::CustodyEvent sealed;
    sealed.kind = fe::CustodyEvent_Kind::Sealed; sealed.actor = "jane";
    sealed.actor_id = efp; sealed.at = "2026-07-02T00:00:00Z";
    sealed.binds = fe::annotations_hash(sent);
    fe::sign_event(fe::append_event(sent.custody, sealed), editor);

    // Author verdicts a note, then builds + signs + seals the return.
    std::vector<fe::Annotation> returned = sent.annotations;
    check("carrier has at least one note to verdict", !returned.empty());
    returned[0].verdict = fe::Verdict::Declined;

    fe::Document ret = fe::make_return_document(sent, returned, "scott", afp,
                                                "2026-07-03T00:00:00Z");
    check("return preserves prior chain + appends one (3 links)", ret.custody.size() == 3);
    check("return continuity: first two event hashes unchanged",
          ret.custody[0].hash == sent.custody[0].hash &&
          ret.custody[1].hash == sent.custody[1].hash);
    fe::sign_event(ret.custody.back(), author);          // author signs the return seal

    fe::save_document_pw(path, ret, "otters unionize discount turnips");
    fe::Document back = fe::open_document_pw(path, "otters unionize discount turnips");

    check("return: chain verifies after file round-trip", fe::verify_chain(back.custody));
    check("return: all three signatures verify (author, editor, author)",
          back.custody.size() == 3 &&
          fe::verify_event_signature(back.custody[0], author.public_key) &&
          fe::verify_event_signature(back.custody[1], editor.public_key) &&
          fe::verify_event_signature(back.custody[2], author.public_key));
    check("return: the author's seal binds the recomputed annotations_hash",
          back.custody[2].binds == fe::annotations_hash(back));
    check("return: the verdict survived the seal round-trip",
          !back.annotations.empty() && back.annotations[0].verdict == fe::Verdict::Declined);
    check("return: the author's seal differs from the editor's (verdict changed the hash)",
          back.custody[2].binds != back.custody[1].binds);

    std::remove(path.c_str());
}

int main() {
    std::printf("folioedit Archive tests\n");
    test_raw_key_file();
    test_passphrase_file();
    test_binds_hashes();
    test_append_event();
    test_capstone();
    test_make_return();
    std::printf("\nfolioedit archive: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
