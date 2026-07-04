// Folio :: DocumentModel::Annotation `verdict` round-trip -- a MIRROR test.
//
// DocumentModel.hpp pulls the whole app (FolioPrefs, ObjectStore, ImagePool,
// gtkmm transitively), so it can't be g++-compiled in the sandbox. This mirror
// carries a byte-copy of Annotation's `verdict`-relevant to_json/from_json so the
// omit-when-empty discipline is provable purely (same tactic as the s99 `source`
// mirror). KEEP IN SYNC with DocumentModel.cpp -- if the real Annotation JSON
// changes, change this too, or the mirror silently drifts.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../folioedit/include TEST_annotation_verdict.cpp ../folioedit/src/Format.cpp ../folioedit/src/Custody.cpp ../folioedit/src/Sha256.cpp -o test_annotation_verdict && ./test_annotation_verdict
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../folioedit/include TEST_annotation_verdict.cpp ../folioedit/src/Format.cpp ../folioedit/src/Custody.cpp ../folioedit/src/Sha256.cpp -o test_annotation_verdict && ./test_annotation_verdict
*/

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
#include "folioedit/Format.hpp"   // s107 — engine Verdict enum, to assert the boundary contract

namespace fe = folioedit;

using json = nlohmann::json;

// ── MIRROR of DocumentModel::Annotation (verdict-relevant fields only) ────────
namespace Verdicts {
inline constexpr const char* kProposed = "proposed";
inline constexpr const char* kAccepted = "accepted";
inline constexpr const char* kDeclined = "declined";
}

struct Annotation {
    int         id          = 0;
    int         range_start = 0;
    int         range_end   = 0;
    std::string text;
    std::string color_hex   = "#fef08a";
    std::string kind        = "Writer";
    std::string created_at;
    std::string source;                   // "" = self/legacy
    std::string verdict;                  // "" = not a proposal

    bool is_proposal()     const { return !verdict.empty(); }
    bool verdict_pending() const { return verdict == "proposed"; }

    json to_json() const {
        json j = {
            {"id",          id},
            {"range_start", range_start},
            {"range_end",   range_end},
            {"text",        text},
            {"color_hex",   color_hex},
            {"kind",        kind},
            {"created_at",  created_at}
        };
        if (!source.empty())  j["source"]  = source;
        if (!verdict.empty()) j["verdict"] = verdict;
        return j;
    }
    void from_json(const json& j) {
        id          = j.value("id",          0);
        range_start = j.value("range_start", 0);
        range_end   = j.value("range_end",   0);
        text        = j.value("text",        "");
        color_hex   = j.value("color_hex",   "#fef08a");
        kind        = j.value("kind",        "Writer");
        created_at  = j.value("created_at",  "");
        source      = j.value("source",      "");
        verdict     = j.value("verdict",     "");
    }
};

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total; if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}

int main() {
    // A self/legacy note (source == "", verdict == "") stays byte-identical: no
    // `source` key, no `verdict` key -- adding the field diffs cleanly on disk.
    Annotation self;
    self.id = 1; self.range_start = 3; self.range_end = 9; self.text = "own note";
    check("self note is not a proposal", !self.is_proposal());
    check("self note omits verdict from JSON",  !self.to_json().contains("verdict"));
    check("self note omits source from JSON",   !self.to_json().contains("source"));

    // An imported proposal: source stamped, verdict "proposed" -> round-trips.
    Annotation prop;
    prop.id = 2; prop.text = "filter verb"; prop.source = "claude";
    prop.verdict = Verdicts::kProposed;
    check("imported proposal IS a proposal",     prop.is_proposal());
    check("proposed is pending",                 prop.verdict_pending());
    check("proposal emits verdict to JSON",      prop.to_json().contains("verdict"));
    Annotation prop2; prop2.from_json(prop.to_json());
    check("proposed verdict survives round-trip", prop2.verdict == "proposed");
    check("proposal round-trips byte-identical",  prop.to_json().dump() == prop2.to_json().dump());

    // Transitions record and survive.
    Annotation acc = prop; acc.verdict = Verdicts::kAccepted;
    Annotation dec = prop; dec.verdict = Verdicts::kDeclined;
    check("accepted is no longer pending", !acc.verdict_pending());
    check("declined is no longer pending", !dec.verdict_pending());
    Annotation acc2; acc2.from_json(acc.to_json());
    Annotation dec2; dec2.from_json(dec.to_json());
    check("accepted survives round-trip", acc2.verdict == "accepted");
    check("declined survives round-trip", dec2.verdict == "declined");

    // A file written BEFORE s107 (no verdict key) reads back as not-a-proposal.
    json legacy = { {"id", 5}, {"range_start", 0}, {"range_end", 2}, {"text", "old"},
                    {"color_hex", "#fef08a"}, {"kind", "Editor"}, {"created_at", ""},
                    {"source", "claude"} };   // imported pre-verdict: source but no verdict
    Annotation from_legacy; from_legacy.from_json(legacy);
    check("pre-s107 file: absent verdict -> \"\"", from_legacy.verdict.empty());

    // s107 boundary contract: the ENGINE's verdict strings must be byte-equal to
    // the MODEL's Verdicts:: constants, or absorb()'s verdict_to_str -> filed
    // annotation -> re-export mapping silently drifts. This is the one test that
    // sees both sides at once (Format.hpp is pure), so it's the drift tripwire.
    check("engine \"proposed\" == model kProposed",
          fe::verdict_to_str(fe::Verdict::Proposed) == std::string(Verdicts::kProposed));
    check("engine \"accepted\" == model kAccepted",
          fe::verdict_to_str(fe::Verdict::Accepted) == std::string(Verdicts::kAccepted));
    check("engine \"declined\" == model kDeclined",
          fe::verdict_to_str(fe::Verdict::Declined) == std::string(Verdicts::kDeclined));
    // ...and the reverse mapping the filed model string feeds back on re-export.
    check("model kAccepted -> engine Accepted",
          fe::verdict_from_str(Verdicts::kAccepted) == fe::Verdict::Accepted);
    check("model kDeclined -> engine Declined",
          fe::verdict_from_str(Verdicts::kDeclined) == fe::Verdict::Declined);

    std::printf("\nannotation verdict mirror: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
