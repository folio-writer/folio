// folioedit :: plain-face archive tests (s18.2/18.3/18.5) -- the trusted-AI
// loop, with NO crypto at all. Exercises the unsealed read/write path, the
// content sniff, and integrity-without-encryption:
//   - save_document_plain -> open_document_plain round-trips the Document
//     (scenes, pass, custody) through readable JSON; the `instructions` block is
//     written and ignored on read.
//   - peek_file_face classifies a plain file (Plain), a sealed-magic file
//     (Sealed), and garbage (Unknown) -- all without a key.
//   - the unsigned `issued` event binds body_hash; recomputing it proves the
//     editor ONLY APPENDED annotations (body_hash stable) and DETECTS a rewrite
//     of the prose (body_hash drifts) -- the s18.5 "verify me" property, on the
//     now-pure SHA. verify_chain holds on the unsigned chain.
//
// Built for the PLAIN face: -DFOLIOEDIT_NO_CRYPTO, links no libcrypto (Archive's
// sealed open/save compile out; only the plain path + hashes remain).
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -DFOLIOEDIT_NO_CRYPTO -I /home/claude/sbox -I ../include TEST_archive_plain.cpp ../src/Archive.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Sha256.cpp -o test_archive_plain && ./test_archive_plain
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -DFOLIOEDIT_NO_CRYPTO -I ../include TEST_archive_plain.cpp ../src/Archive.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Sha256.cpp -o test_archive_plain && ./test_archive_plain
*/

#include "folioedit/Archive.hpp"
#include "folioedit/Custody.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}

static void write_raw(const std::string& path, const std::string& s) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

// A minimal pass: two scenes, a briefing, and an unsigned `issued` event that
// binds body_hash (what Folio's plain export writes -- s18.5).
static fe::Document make_plain_pass() {
    fe::Document doc;
    doc.project_id = "prj_demo"; doc.project_title = "The Otter Papers";
    doc.pass.id = "pass_1"; doc.pass.source = "claude";
    doc.pass.kinds = {"Proofreader", "Editor", "Writer"};
    doc.pass.rules = "surface, don't verdict";

    fe::Scene s1; s1.iid = "scn_aaa"; s1.title = "Open"; s1.order = 0;
    s1.text = "<p>The tide went out at dawn.</p>";
    fe::Scene s2; s2.iid = "scn_bbb"; s2.title = "Turn"; s2.order = 1;
    s2.text = "<p>She counted seven shells.</p>";
    doc.scenes = {s1, s2};

    fe::CustodyEvent issued;
    issued.kind = fe::CustodyEvent_Kind::Issued;
    issued.actor = "scott"; issued.actor_id = "tofu:local";
    issued.at = "2026-07-01T09:00:00Z";
    issued.binds = fe::body_hash(doc);        // bind the sent prose
    fe::append_event(doc.custody, issued);    // unsigned -- no Ed25519 in the plain face
    return doc;
}

static void test_roundtrip() {
    const std::string path = "/tmp/fe_plain_roundtrip.folioedit";
    fe::Document doc = make_plain_pass();
    fe::save_document_plain(path, doc);
    fe::Document back = fe::open_document_plain(path);

    check("round-trip project id",   back.project_id == doc.project_id);
    check("round-trip scene count",  back.scenes.size() == 2);
    check("round-trip scene text",   back.scenes[1].text == doc.scenes[1].text);
    check("round-trip pass kinds",   back.pass.kinds.size() == 3 && back.pass.kinds[0] == "Proofreader");
    check("round-trip custody chain verifies", fe::verify_chain(back.custody));
    check("unsigned issued event has empty signature",
          back.custody.size() == 1 && back.custody[0].signature.empty());

    // The readable file actually contains the instructions block + is JSON.
    std::ifstream f(path, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    check("plain file is JSON (leads with '{')", !body.empty() && body[0] == '{');
    check("plain file carries an instructions block",
          body.find("\"instructions\"") != std::string::npos);
}

static void test_sniff() {
    const std::string plain = "/tmp/fe_sniff_plain.folioedit";
    const std::string sealed = "/tmp/fe_sniff_sealed.folioedit";
    const std::string junk = "/tmp/fe_sniff_junk.folioedit";
    fe::save_document_plain(plain, make_plain_pass());
    write_raw(sealed, std::string("FOLIOEDIT") + std::string(40, '\x01'));  // magic + framing
    write_raw(junk, "not a folioedit file at all");

    check("sniff: plain file -> Plain",   fe::peek_file_face(plain)  == fe::FileFace::Plain);
    check("sniff: sealed magic -> Sealed", fe::peek_file_face(sealed) == fe::FileFace::Sealed);
    check("sniff: garbage -> Unknown",    fe::peek_file_face(junk)   == fe::FileFace::Unknown);
    check("sniff: leading whitespace before '{' still Plain",
          [&]{ write_raw(junk, "\n\t  {\"project\":{}}"); return fe::peek_file_face(junk) == fe::FileFace::Plain; }());
}

static void test_only_appended_integrity() {
    fe::Document doc = make_plain_pass();
    const std::string bound = doc.custody[0].binds;

    // The editor appends annotations (the legal move) -- body_hash is unaffected,
    // so the issued bind still matches: proves "only appended".
    fe::Annotation a; a.scene_iid = "scn_aaa"; a.kind = "Editor";
    a.quote = "tide went out"; a.text = "consider 'ebbed'";
    doc.annotations.push_back(a);
    check("append annotation leaves body_hash intact (only-appended proven)",
          fe::body_hash(doc) == bound);

    // A rewrite of the prose (the forbidden move) -- body_hash drifts, so Absorb
    // can catch it against the issued bind.
    doc.scenes[0].text = "<p>The tide RUSHED out at dawn.</p>";
    check("rewriting a scene drifts body_hash (rewrite detected)",
          fe::body_hash(doc) != bound);
}

int main() {
    test_roundtrip();
    test_sniff();
    test_only_appended_integrity();
    std::printf("\nfolioedit plain archive: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
