// ─────────────────────────────────────────────────────────────────────────────
// TEST_editor_identity.cpp — s108: robust editor-label matching.
//
// A recipient/source is a human-typed label that drifts by case and spacing across
// passes; the interchange scopes a return to "this editor's notes" on that label,
// so the match must be robust or one editor fragments into several. Proves
// editor_key normalization (trim + collapse + ASCII-lower, UTF-8 preserved),
// same_editor, and the ledger's distinct_editors dropdown feed (dedup by key,
// most-recent display form, hidden excluded).
//
// Pure (STL + nlohmann) -> sandbox-provable under the full strict flags.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_editor_identity.cpp ../src/InterchangeLedger.cpp -o /tmp/test_editor_identity && /tmp/test_editor_identity
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_editor_identity.cpp ../src/InterchangeLedger.cpp -o /tmp/test_editor_identity && /tmp/test_editor_identity
*/
#include "InterchangeLedger.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void ok(bool c, const std::string& what) {
    if (c) ++g_pass; else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

int main() {
    // ── editor_key normalization ───────────────────────────────────────────────
    ok(editor_key("James")       == "james",       "lowercases ASCII");
    ok(editor_key("  james  ")   == "james",       "trims ends");
    ok(editor_key("James  Smith")== "james smith", "collapses internal whitespace");
    ok(editor_key("\tJAMES\n")   == "james",       "trims tabs/newlines + lowers");
    ok(editor_key("")            == "",            "empty stays empty");
    ok(editor_key("   ")         == "",            "all-whitespace -> empty");
    // UTF-8 preserved: only ASCII A-Z shift; the accented byte is untouched.
    ok(editor_key("Jos\xC3\xA9") == "jos\xC3\xA9", "UTF-8 bytes preserved (Jose w/ accent)");

    // ── same_editor: the Scott's two-editor case ──────────────────────────────
    ok(same_editor("Scott", "scott"),          "Scott == scott (case)");
    ok(same_editor("James Smith", "james  smith"), "spacing + case ignored");
    ok(same_editor(" jane", "jane "),          "surrounding space ignored");
    ok(!same_editor("James", "Sarah"),         "different NAMES stay distinct");
    ok(!same_editor("England Ed", "New Hampshire Ed"), "two real editors distinct");

    // ── distinct_editors: the export dropdown feed ─────────────────────────────
    {
        InterchangeLedger led;
        auto mk = [](const std::string& id, const std::string& rcpt, bool hidden) {
            LedgerEntry e; e.id = id; e.recipient = rcpt; e.hidden = hidden; return e;
        };
        led.record_sent(mk("p1", "James",  false));
        led.record_sent(mk("p2", "Sarah",  false));
        led.record_sent(mk("p3", "james",  false));   // same editor as p1, new casing
        led.record_sent(mk("p4", "Bob",    true));     // hidden

        auto vis = led.distinct_editors(false);
        ok(vis.size() == 2, "two distinct visible editors (James/james folded)");
        ok(vis[0] == "james", "folded editor shows MOST-RECENT display form");
        ok(vis[1] == "Sarah", "second editor preserved, first-appearance order");

        auto all = led.distinct_editors(true);
        ok(all.size() == 3 && all[2] == "Bob", "include_hidden surfaces hidden editor");
    }

    std::cout << "\neditor identity: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
