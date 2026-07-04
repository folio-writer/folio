//
// TEST_interchange_ledger_hidden.cpp -- Folio :: LedgerEntry::hidden + set_hidden()
//
// Proves the "permanent register, hide the stale/mistaken ones" model: a pass
// defaults to visible, set_hidden(id,true/false) flips it, an unknown id is a
// no-op reporting false, and the flag is serialized ONLY while true (an untouched
// ledger diffs clean) yet survives dump()/parse() when set. Hiding never removes:
// hide + remove are independent, and a hidden entry is still findable, still
// counts toward size(), and can be un-hidden or (deliberately) removed.
//
// Pure STL + nlohmann -- compiles + runs in the sandbox with strict flags.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_interchange_ledger_hidden.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger_hidden && /tmp/test_ledger_hidden
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_interchange_ledger_hidden.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger_hidden && /tmp/test_ledger_hidden
*/

#include "InterchangeLedger.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

static LedgerEntry mk(const std::string& id, const std::string& who) {
    LedgerEntry e;
    e.id         = id;
    e.recipient  = who;
    e.created_at = "2026-06-01T09:00:00Z";
    e.inventory  = { {"iid-a", "Chapter One"} };
    return e;
}

static std::size_t count_hidden(const InterchangeLedger& l) {
    std::size_t n = 0;
    for (const auto& e : l.entries()) if (e.hidden) ++n;
    return n;
}

int main() {
    InterchangeLedger led;
    led.record_sent(mk("p1", "Jane"));
    led.record_sent(mk("p2", "Omar"));

    // ── default is visible ───────────────────────────────────────────────────
    check("default not hidden p1", led.find("p1") && !led.find("p1")->hidden);
    check("default not hidden p2", led.find("p2") && !led.find("p2")->hidden);
    check("no hidden yet",         count_hidden(led) == 0);

    // ── hide one ─────────────────────────────────────────────────────────────
    check("set_hidden true reports true", led.set_hidden("p1", true));
    check("p1 now hidden",  led.find("p1") && led.find("p1")->hidden);
    check("p2 still visible", led.find("p2") && !led.find("p2")->hidden);
    check("one hidden",     count_hidden(led) == 1);

    // ── hiding is NOT removing ───────────────────────────────────────────────
    check("hidden still present", led.find("p1") != nullptr);
    check("size unchanged by hide", led.size() == 2);

    // ── unhide ───────────────────────────────────────────────────────────────
    check("set_hidden false reports true", led.set_hidden("p1", false));
    check("p1 visible again", led.find("p1") && !led.find("p1")->hidden);
    check("none hidden",      count_hidden(led) == 0);

    // ── unknown id is a clean no-op ──────────────────────────────────────────
    check("set_hidden unknown false", led.set_hidden("nope", true) == false);

    // ── serialization: omit while false ──────────────────────────────────────
    {
        LedgerEntry e = mk("v", "Visible");
        json j = e.to_json();
        check("visible omits hidden key", !j.contains("hidden"));
    }
    {
        LedgerEntry e = mk("h", "Hidden");
        e.hidden = true;
        json j = e.to_json();
        check("hidden writes hidden=true", j.contains("hidden") && j["hidden"] == true);
    }

    // ── round-trip through dump()/parse() ────────────────────────────────────
    led.set_hidden("p2", true);
    InterchangeLedger reloaded = InterchangeLedger::parse(led.dump());
    check("reload keeps p2 hidden",  reloaded.find("p2") && reloaded.find("p2")->hidden);
    check("reload keeps p1 visible", reloaded.find("p1") && !reloaded.find("p1")->hidden);
    check("reload hidden count one", count_hidden(reloaded) == 1);

    // ── hide + remove coexist: delete a hidden entry ─────────────────────────
    check("remove hidden p2 true", reloaded.remove("p2"));
    check("p2 gone after remove",  reloaded.find("p2") == nullptr);
    check("p1 remains",            reloaded.find("p1") != nullptr);

    std::cout << (g_fail == 0 ? "ALL PASS" : "SOME FAIL")
              << "  (" << g_pass << " passed, " << g_fail << " failed)\n";
    return g_fail == 0 ? 0 : 1;
}
