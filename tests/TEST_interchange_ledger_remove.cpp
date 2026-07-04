//
// TEST_interchange_ledger_remove.cpp -- Folio :: InterchangeLedger::remove()
//
// Proves the new local-drop mutation on the author-side ledger: remove(id) erases
// exactly the named pass and nothing else, reports whether it removed anything,
// leaves the other entries and their order intact, is a clean no-op on an unknown
// id (and on an already-emptied book), and round-trips through dump()/parse() so a
// removed pass does not resurrect on the next save/load. remove() touches only the
// author's private record -- never a sealed .folioedit already sent -- so this is
// purely a container-mutation contract.
//
// Pure STL + nlohmann -- compiles + runs in the sandbox with strict flags.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_interchange_ledger_remove.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger_remove && /tmp/test_ledger_remove
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_interchange_ledger_remove.cpp src/InterchangeLedger.cpp -o /tmp/test_ledger_remove && /tmp/test_ledger_remove
*/

#include "InterchangeLedger.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

static LedgerEntry mk(const std::string& id, const std::string& who,
                      const std::string& created) {
    LedgerEntry e;
    e.id         = id;
    e.recipient  = who;
    e.phrase     = "otters unionize discount turnips";
    e.created_at = created;
    e.body_hash  = "hash-" + id;
    e.inventory  = { {"iid-a", "Chapter One"}, {"iid-b", "Chapter Two"} };
    return e;
}

static std::vector<std::string> ids(const InterchangeLedger& l) {
    std::vector<std::string> v;
    for (const auto& e : l.entries()) v.push_back(e.id);
    return v;
}

int main() {
    // ── build a three-pass book ──────────────────────────────────────────────
    InterchangeLedger led;
    led.record_sent(mk("p1", "Jane",  "2026-06-01T09:00:00Z"));
    led.record_sent(mk("p2", "Omar",  "2026-06-02T09:00:00Z"));
    led.record_sent(mk("p3", "Priya", "2026-06-03T09:00:00Z"));
    check("seeded three", led.size() == 3);

    // ── remove the middle one ────────────────────────────────────────────────
    check("remove p2 reports true", led.remove("p2") == true);
    check("size now two",           led.size() == 2);
    check("p2 is gone",             led.find("p2") == nullptr);
    check("p1 survives",            led.find("p1") != nullptr);
    check("p3 survives",            led.find("p3") != nullptr);
    check("order preserved",        (ids(led) == std::vector<std::string>{"p1", "p3"}));

    // ── unknown id is a clean no-op ──────────────────────────────────────────
    check("remove unknown reports false", led.remove("nope") == false);
    check("size unchanged after no-op",   led.size() == 2);

    // ── double-remove: second call is a no-op ────────────────────────────────
    check("remove p1 true",        led.remove("p1") == true);
    check("remove p1 again false", led.remove("p1") == false);
    check("only p3 left",          (ids(led) == std::vector<std::string>{"p3"}));

    // ── remove the last entry -> empty, and empty is well-behaved ────────────
    check("remove p3 true",             led.remove("p3") == true);
    check("book empty",                 led.empty());
    check("remove from empty is false", led.remove("p3") == false);

    // ── removed passes do NOT resurrect through dump()/parse() ────────────────
    InterchangeLedger led2;
    led2.record_sent(mk("k1", "Jane", "2026-06-01T09:00:00Z"));
    led2.record_sent(mk("k2", "Omar", "2026-06-02T09:00:00Z"));
    led2.remove("k1");
    InterchangeLedger reloaded = InterchangeLedger::parse(led2.dump());
    check("reload size is one",     reloaded.size() == 1);
    check("removed k1 stays gone",  reloaded.find("k1") == nullptr);
    check("kept k2 present",        reloaded.find("k2") != nullptr);

    // ── remove doesn't disturb a neighbour's fields ──────────────────────────
    InterchangeLedger led3;
    led3.record_sent(mk("a", "Jane", "2026-06-01T09:00:00Z"));
    led3.record_sent(mk("b", "Omar", "2026-06-02T09:00:00Z"));
    led3.mark_returned("b", "2026-06-05T12:00:00Z", 7);
    led3.remove("a");
    const LedgerEntry* b = led3.find("b");
    check("neighbour intact: status",  b && b->status == PassStatus::Returned);
    check("neighbour intact: count",   b && b->annotation_count == 7);
    check("neighbour intact: recip",   b && b->recipient == "Omar");

    std::cout << (g_fail == 0 ? "ALL PASS" : "SOME FAIL")
              << "  (" << g_pass << " passed, " << g_fail << " failed)\n";
    return g_fail == 0 ? 0 : 1;
}
