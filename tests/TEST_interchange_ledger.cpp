// ─────────────────────────────────────────────────────────────────────────────
// TEST_interchange_ledger.cpp — Folio :: InterchangeLedger (pure, s102)
//
// The author-side "what went out / what came home" book. Proves the record
// lifecycle (record_sent append + overwrite-by-id, mark_returned flip), the
// "is the file back the file I sent?" body_hash reconciliation, and a full JSON
// round-trip through the .folio bundle form (dump -> parse), including omit-
// while-Sent of returned_at and legacy-tolerant reads.
//
// Pure STL + nlohmann — sandbox-compilable, no gtkmm, no engine.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include -I /home/claude/sbox tests/TEST_interchange_ledger.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger && /tmp/test_ledger
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_interchange_ledger.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger && /tmp/test_ledger
*/

#include "InterchangeLedger.hpp"

#include <iostream>
#include <string>

namespace F = Folio;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

static F::LedgerEntry make_entry(const std::string& id, const std::string& who) {
    F::LedgerEntry e;
    e.id         = id;
    e.recipient  = who;
    e.phrase     = "otters unionize discount turnips";
    e.created_at = "2026-07-01T10:00:00Z";
    e.body_hash  = "abc123def456";
    e.inventory  = { {"scn_4f2a", "The confession"}, {"scn_9b1c", "Aftermath"} };
    return e;
}

int main() {
    // ── record_sent: append, then overwrite by id ────────────────────────────
    {
        F::InterchangeLedger led;
        led.record_sent(make_entry("pass_001", "Jane Doe"));
        led.record_sent(make_entry("pass_002", "Sam Ray"));
        check("two passes recorded", led.size() == 2);
        check("find by id", led.find("pass_002") != nullptr &&
                            led.find("pass_002")->recipient == "Sam Ray");
        check("unknown id -> nullptr", led.find("pass_999") == nullptr);

        // re-issue pass_001 -> overwrite in place, no new row
        F::LedgerEntry reissue = make_entry("pass_001", "Jane D. (revised)");
        led.record_sent(reissue);
        check("re-issue overwrites, count unchanged", led.size() == 2);
        check("re-issue updated the record",
              led.find("pass_001")->recipient == "Jane D. (revised)");
    }

    // ── mark_returned: flip status, stamp when + count ───────────────────────
    {
        F::InterchangeLedger led;
        led.record_sent(make_entry("pass_001", "Jane Doe"));
        check("starts Sent", led.find("pass_001")->status == F::PassStatus::Sent);

        bool ok = led.mark_returned("pass_001", "2026-07-05T14:30:00Z", 7);
        check("mark_returned true on known id", ok);
        const F::LedgerEntry* e = led.find("pass_001");
        check("status is Returned", e->status == F::PassStatus::Returned);
        check("returned_at stamped", e->returned_at == "2026-07-05T14:30:00Z");
        check("annotation_count stamped", e->annotation_count == 7);

        check("mark_returned false on unknown id",
              !led.mark_returned("nope", "x", 0));
    }

    // ── body_matches: "is this the file I sent?" ─────────────────────────────
    {
        F::InterchangeLedger led;
        led.record_sent(make_entry("pass_001", "Jane Doe"));   // body_hash abc123def456
        check("matching hash",     led.body_matches("pass_001", "abc123def456"));
        check("drifted hash fails", !led.body_matches("pass_001", "deadbeef"));
        check("unknown id fails",   !led.body_matches("nope", "abc123def456"));

        // empty stored hash never matches
        F::LedgerEntry blank = make_entry("pass_002", "Sam");
        blank.body_hash = "";
        led.record_sent(blank);
        check("empty stored hash never matches",
              !led.body_matches("pass_002", ""));
    }

    // ── JSON round-trip through the bundle form ──────────────────────────────
    {
        F::InterchangeLedger led;
        led.record_sent(make_entry("pass_001", "Jane Doe"));
        led.record_sent(make_entry("pass_002", "Sam Ray"));
        led.mark_returned("pass_002", "2026-07-06T09:00:00Z", 3);

        std::string text = led.dump();
        F::InterchangeLedger back = F::InterchangeLedger::parse(text);

        check("round-trip preserves count", back.size() == 2);
        const F::LedgerEntry* a = back.find("pass_001");
        const F::LedgerEntry* b = back.find("pass_002");
        check("Sent pass survives",     a && a->status == F::PassStatus::Sent);
        check("phrase survives",        a && a->phrase == "otters unionize discount turnips");
        check("inventory survives",     a && a->inventory.size() == 2 &&
                                        a->inventory[0].iid == "scn_4f2a" &&
                                        a->inventory[0].title == "The confession");
        check("Returned pass survives", b && b->status == F::PassStatus::Returned &&
                                        b->returned_at == "2026-07-06T09:00:00Z" &&
                                        b->annotation_count == 3);
        check("body_hash survives",     a && a->body_hash == "abc123def456");
    }

    // ── omit-while-Sent + legacy tolerance ───────────────────────────────────
    {
        F::InterchangeLedger led;
        led.record_sent(make_entry("pass_001", "Jane Doe"));   // Sent
        F::json j = led.to_json();
        const F::json& e0 = j["passes"][0];
        check("returned_at omitted while Sent", !e0.contains("returned_at"));
        check("schema stamped", j.value("folio_interchange_schema", 0) == 1);

        // a hand-written minimal blob (no status, no inventory) reads as Sent/empty
        F::json legacy = F::json::parse(
            R"({"passes":[{"id":"p","recipient":"x","created_at":"t"}]})");
        F::InterchangeLedger l2;
        l2.from_json(legacy);
        const F::LedgerEntry* le = l2.find("p");
        check("legacy blob reads", le != nullptr);
        check("legacy defaults to Sent", le && le->status == F::PassStatus::Sent);
        check("legacy empty inventory", le && le->inventory.empty());
        check("legacy count defaults 0", le && le->annotation_count == 0);
    }

    // ── empty text parses to an empty ledger (no ledger yet in a fresh project) ─
    {
        F::InterchangeLedger led = F::InterchangeLedger::parse("");
        check("empty text -> empty ledger", led.empty());
    }

    std::cout << "TEST_interchange_ledger: " << g_pass << "/" << (g_pass + g_fail)
              << " passed\n";
    return g_fail == 0 ? 0 : 1;
}
