// ─────────────────────────────────────────────────────────────────────────────
// TEST_annotation_source.cpp — s102 fold-in check for Annotation.source
//
// A MIRROR test: it re-declares the exact Annotation JSON round-trip logic from
// DocumentModel.cpp (which cannot be sandbox-compiled — it pulls FolioPrefs /
// ObjectStore / gtkmm through its include chain) so the pure serialization
// contract can be proven here with strict flags before Scott compiles the real
// tree. If this mirror drifts from DocumentModel::Annotation, update BOTH.
//
// Proves: omit-when-empty on write, absent -> "" (self/legacy) on read, and a
// stamped source ("claude") survives a full round-trip. (DESIGN_editorialization
// §3 / §18.1.)
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox tests/TEST_annotation_source.cpp -o /tmp/test_ann_source && /tmp/test_ann_source
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow tests/TEST_annotation_source.cpp -o /tmp/test_ann_source && /tmp/test_ann_source
*/

#include <cassert>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── mirror of DocumentModel::Annotation (fields + JSON only) ─────────────────
struct Annotation {
    int         id          = 0;
    int         range_start = 0;
    int         range_end   = 0;
    std::string text;
    std::string color_hex   = "#fef08a";
    std::string kind        = "Writer";
    std::string created_at;
    std::string source;                   // "" = self/legacy

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
        if (!source.empty()) j["source"] = source;
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
    }
};

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const std::string& what) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

int main() {
    // 1. Empty source is omitted on the wire (byte-clean diff for legacy files).
    {
        Annotation a;
        a.id = 3; a.range_start = 10; a.range_end = 20; a.text = "note";
        json j = a.to_json();
        check(!j.contains("source"), "empty source omitted from JSON");
    }

    // 2. A legacy blob (no "source" key) reads back as "" = self/legacy.
    {
        json legacy = {
            {"id", 1}, {"range_start", 0}, {"range_end", 5},
            {"text", "old"}, {"color_hex", "#fef08a"},
            {"kind", "Writer"}, {"created_at", "2024-01-01T00:00:00Z"}
        };
        Annotation a;
        a.from_json(legacy);
        check(a.source.empty(), "absent source -> \"\" (self/legacy)");
        check(a.text == "old", "legacy fields still read");
    }

    // 3. A stamped source survives a full round-trip.
    {
        Annotation a;
        a.id = 7; a.range_start = 4; a.range_end = 9;
        a.text = "eye colour drifts ch3->ch9";
        a.kind = "Editor"; a.created_at = "2026-07-01T12:00:00Z";
        a.source = "claude";
        json j = a.to_json();
        check(j.contains("source"), "non-empty source written");

        Annotation b;
        b.from_json(j);
        check(b.source == "claude",   "source round-trips");
        check(b.kind   == "Editor",   "kind round-trips beside source");
        check(b.text   == a.text,     "text round-trips beside source");
        check(b.range_start == 4 && b.range_end == 9, "range round-trips");
    }

    // 4. Explicit empty-string source is treated the same as absent on re-save.
    {
        Annotation a;
        a.source = "";
        json j = a.to_json();
        check(!j.contains("source"), "explicit empty source also omitted");
    }

    std::cout << "TEST_annotation_source: " << g_pass << "/" << (g_pass + g_fail)
              << " passed\n";
    return g_fail == 0 ? 0 : 1;
}
