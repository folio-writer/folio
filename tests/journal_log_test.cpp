// journal_log_test.cpp — proves the deck-log lifecycle (JournalLog).
//
// Covers every rule that makes the journal a historical account: the always-
// present draft, commit-on-first-input with an IMMUTABLE stamp, accept-locks-and-
// opens-fresh, accept-is-a-no-op on an empty draft, soft-delete-retains an
// accepted record, delete-restarts the draft clock, newest-first working view,
// and export with/without deleted. GTK-free; std-lib only.

/*
  # sandbox (this box)
  g++ -std=c++20 -Wall -Wextra -I../include journal_log_test.cpp ../src/JournalLog.cpp -o /tmp/jlog_test && /tmp/jlog_test

  # Fedora (Scott's box)
  clang++ -std=c++20 -Wall -Wextra -Iinclude tests/journal_log_test.cpp src/JournalLog.cpp -o /tmp/jlog_test && /tmp/jlog_test
*/

#include "JournalLog.hpp"
#include <cstdio>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
  if (ok) { ++g_pass; }
  else    { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

int main() {
  // ── a fresh log is one waiting, uncommitted draft ──
  {
    JournalLog L;
    check(L.all().size() == 1, "fresh: single entry");
    check(!L.draft().committed(), "fresh: draft uncommitted");
    check(!L.draft().accepted, "fresh: draft not accepted");
    check(L.accepted_view().empty(), "fresh: nothing in working view");
    check(L.export_view(true).empty(), "fresh: nothing to export");
  }

  // ── commit stamps the start time; the stamp is IMMUTABLE ──
  {
    JournalLog L;
    L.commit_draft("2026-06-24T22:42:07");
    check(L.draft().committed(), "commit: draft now committed");
    check(L.draft().iso_dt == "2026-06-24T22:42:07", "commit: stamp set");
    L.commit_draft("2026-06-24T23:10:00"); // later clock must NOT move it
    check(L.draft().iso_dt == "2026-06-24T22:42:07", "commit: stamp immutable");
  }

  // ── accept is a no-op on an empty draft (no empty records ever) ──
  {
    JournalLog L;
    check(!L.accept_draft(), "accept: refused on uncommitted draft");
    check(L.all().size() == 1, "accept: no empty record created");
  }

  // ── accept locks the draft and opens a fresh one ──
  {
    JournalLog L;
    L.commit_draft("2026-06-21T14:30:00");
    L.draft().text = "Found the voice for Virgil-as-carny";
    check(L.accept_draft(), "accept: succeeds on committed draft");
    check(L.all().size() == 2, "accept: record + fresh draft");
    check(L.all()[0].accepted, "accept: prior entry locked");
    check(!L.draft().committed(), "accept: new draft is fresh/uncommitted");
    check(L.draft().text.empty(), "accept: new draft is blank");
  }

  // ── working view is newest-first, draft excluded ──
  {
    JournalLog L;
    L.commit_draft("2026-06-21T14:30:00"); L.draft().text = "a"; L.accept_draft();
    L.commit_draft("2026-06-22T08:15:00"); L.draft().text = "b"; L.accept_draft();
    L.commit_draft("2026-06-23T22:05:00"); L.draft().text = "c"; L.accept_draft();
    auto v = L.accepted_view();
    check(v.size() == 3, "view: three accepted records");
    check(L.all()[v[0]].text == "c", "view: newest first (c)");
    check(L.all()[v[1]].text == "b", "view: then b");
    check(L.all()[v[2]].text == "a", "view: then a");
    check(L.all().size() == 4, "view: plus the trailing draft");
  }

  // ── soft-delete an accepted record: retained, hidden, export-selectable ──
  {
    JournalLog L;
    L.commit_draft("2026-06-21T14:30:00"); L.draft().text = "keep"; L.accept_draft();
    L.commit_draft("2026-06-22T08:15:00"); L.draft().text = "gone"; L.accept_draft();
    L.soft_delete(1); // the "gone" record
    check(L.all()[1].deleted, "delete: record marked deleted");
    check(L.all().size() == 3, "delete: record retained (not erased)");
    auto v = L.accepted_view();
    check(v.size() == 1 && L.all()[v[0]].text == "keep", "delete: hidden from view");
    check(L.export_view(false).size() == 1, "delete: export-without drops it");
    check(L.export_view(true).size() == 2, "delete: export-with keeps it");
  }

  // ── delete on the draft clears it and restarts the clock ──
  {
    JournalLog L;
    L.commit_draft("2026-06-24T09:00:00");
    L.draft().text = "half a thought I want gone";
    L.soft_delete(L.draft_index());
    check(!L.draft().committed(), "draft-delete: clock restarted (uncommitted)");
    check(L.draft().text.empty(), "draft-delete: text cleared");
    check(L.all().size() == 1, "draft-delete: still one waiting draft");
  }

  // ── the uncommitted draft never exports ──
  {
    JournalLog L;
    L.commit_draft("2026-06-21T14:30:00"); L.draft().text = "real"; L.accept_draft();
    // draft now fresh + uncommitted
    check(L.export_view(true).size() == 1, "export: skips the waiting draft");
  }

  std::printf("\nJournalLog: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
