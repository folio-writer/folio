#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// JournalLog — the deck-log record model for the journal (s55 rebuild).
//
// The journal is NOT a prose blob; it is a ship's deck log: an append-only list
// of dated records plus exactly one live DRAFT at the bottom of the data (shown
// pinned at the TOP of the surface). The conviction is a historical account —
// the purest form of writing — so the rules are a watch-stander's, not an
// editor's:
//
//   • There is always exactly one draft (the trailing entry). It waits with the
//     clock ticking; the stamp is NOT set until the writer's first real input.
//   • commit_draft() stamps the start time on first input and is IDEMPOTENT —
//     an entry's time is when it was begun, and it never changes again.
//   • accept_draft() fires the clay: the draft becomes locked record and a fresh
//     uncommitted draft opens. Accept is the only promotion (button / hotkey).
//   • Accepted entries are immutable. Corrections are NOT edits — they are new
//     entries that LINK back (handled by the app's existing link system).
//   • Delete is SOFT: an accepted entry is marked deleted (retained underneath,
//     hidden from the working view, selectable at export). Deleting the draft
//     just clears it back to a waiting, uncommitted draft (the clock restarts).
//
// This whole file is GTK-free and std-lib-only, so the lifecycle is proved in a
// sandbox before any surface is cut. Storage/serialization is a later slice;
// title/★ derivation already lives (and is tested) in Journal.hpp.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstddef>
#include <string>
#include <vector>

namespace Folio {

// One log record. The Title is the first line of `text` (a leading ★ stars it —
// "decoration is the data", as in Journal.hpp); tags are the concept chips.
struct JLogEntry {
  std::string iso_dt;             // immutable start time; "" == uncommitted draft
  std::string text;               // full prose (first line = Title)
  std::vector<std::string> tags;  // concept chips
  std::string accent;             // card-spine colour token ("" = default amber)
  std::string mood;               // one emoji ("" = none)
  bool accepted = false;          // locked record (fired clay)
  bool deleted = false;           // soft-deleted (retained, hidden, export-opt)

  // An entry is "committed" once its start time is stamped (first real input).
  bool committed() const { return !iso_dt.empty(); }
};

class JournalLog {
public:
  // A fresh log is a single waiting, uncommitted draft.
  JournalLog();

  // ── lifecycle ──────────────────────────────────────────────────────────────

  // First real input: stamp the draft's start time. IDEMPOTENT — once a draft is
  // committed its stamp is permanent, so calling again (or with a later clock)
  // never moves it. `now_iso` is supplied by the caller (the surface's clock).
  void commit_draft(const std::string& now_iso);

  // Lock the draft as record and open a fresh uncommitted draft beneath it.
  // No-op (returns false) if the draft is uncommitted — there is nothing to
  // accept, and the log never accrues empty records.
  bool accept_draft();

  // Soft-delete entry i. An accepted record is marked deleted (retained, hidden).
  // The draft itself is cleared back to a fresh uncommitted draft — "delete on the
  // draft restarts the clock." Out-of-range i is ignored.
  void soft_delete(std::size_t i);

  // ── access ───────────────────────────────────────────────────────────────

  // The live draft is always the trailing entry.
  std::size_t draft_index() const { return m_entries.size() - 1; }
  JLogEntry& draft() { return m_entries.back(); }
  const JLogEntry& draft() const { return m_entries.back(); }

  const std::vector<JLogEntry>& all() const { return m_entries; }

  // Accepted records for the working view, NEWEST FIRST, excluding soft-deleted.
  // (The draft is shown separately, pinned at the top of the surface.)
  std::vector<std::size_t> accepted_view() const;

  // Indices for export. include_deleted=false drops soft-deleted records (the
  // clean reading copy); true returns the complete honest log. Chronological.
  std::vector<std::size_t> export_view(bool include_deleted) const;

  // ── serialization (the node body cell, like the MM canvas's CMMDoc) ────────
  // JSON is the on-disk form; the export-to-prose rendering is a separate seam.
  // The empty/uncommitted trailing draft is NOT written (a fresh one is always
  // reconstructed on load) — but a committed, not-yet-accepted draft IS written,
  // so in-progress writing survives a close/reopen.
  std::string to_json() const;

  // Rebuild a log from a stored body. Empty/blank -> a fresh waiting log. Valid
  // JSON -> the saved records. Anything else (e.g. a legacy prose body) is
  // wrapped LOSSLESSLY as one accepted record so no writing is ever destroyed by
  // a format mismatch. Always guarantees a trailing draft (the class invariant).
  static JournalLog from_json(const std::string& body);

private:
  std::vector<JLogEntry> m_entries; // chronological; back() is always the draft
};

} // namespace Folio
