#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — InterchangeLedger.hpp
//
// The AUTHOR-SIDE record of editorial passes: what went out, to whom, under
// which passphrase, and what came back. It is the practical index the custody
// chain inside the .folioedit file cannot be — that chain travels and is sealed;
// this book stays home.
//
//   Custody chain  (folioedit::CustodyEvent)  — inside the sealed file, forensic,
//                                               travels with it.
//   Interchange ledger (this)                 — on the author's machine, private,
//                                               NEVER travels. The "which passphrase
//                                               opens the thing Jane sent back, what
//                                               exactly I sent her, and has it come
//                                               home yet" book.
//
// Lives PER-PROJECT, inside the .folio bundle (Folio writes dump() to the bundle,
// loads via parse()), so a project carries its own editorial history and survives
// a machine move.
//
// The stored `phrase` is the generated passphrase in the clear. That is fine: the
// threat model (DESIGN_editorialization §16) is an untrusted *editor*; the author
// already holds the whole manuscript, so their own book of passphrases is not a
// new exposure. Flagged so it's a decision, not an accident.
//
// PURE: STL + nlohmann only — no gtkmm, no DocumentModel, no folioedit engine.
// Folio's export glue fills an entry from DocumentModel + the issued custody event;
// its import glue flips it to Returned. This layer just holds and (de)serializes
// the record, so it is sandbox-testable end to end. (s102 interchange.)
// ─────────────────────────────────────────────────────────────────────────────
#include <cstddef>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Folio {

using json = nlohmann::json;

inline constexpr int FOLIO_INTERCHANGE_SCHEMA = 1;

// A pass is Sent when the file goes out, Returned once Folio re-absorbs it.
// (Room to grow — e.g. Failed/Expired — without changing the shape.)
enum class PassStatus { Sent, Returned };

std::string pass_status_to_str(PassStatus s);
PassStatus  pass_status_from_str(const std::string& s);

// One scene that went out, recorded by its stable iid + a human title for the
// report. (Titles can drift; the iid is the anchor.)
struct LedgerScene {
    std::string iid;
    std::string title;
};

// One editorial pass.
struct LedgerEntry {
    std::string id;                       // == the .folioedit pass.id
    std::string recipient;                // human label; == the `source` stamped
                                          //   on this pass's annotations
    std::string phrase;                   // the generated passphrase (see header note)
    std::string created_at;               // ISO-8601 — when sent
    std::string body_hash;                // the issued custody event's binds hash
                                          //   (recompute the returned body vs this
                                          //    to prove "this is the file I sent")
    std::vector<LedgerScene> inventory;   // scenes that went out

    PassStatus  status = PassStatus::Sent;
    std::string returned_at;              // ISO-8601 — when re-absorbed ("" until then)
    int         annotation_count = 0;     // annotations filed on import (0 until then)

    json to_json() const;
    void from_json(const json& j);
};

class InterchangeLedger {
public:
    // ── queries ──────────────────────────────────────────────────────────────
    const std::vector<LedgerEntry>& entries() const { return m_entries; }
    std::size_t size() const { return m_entries.size(); }
    bool empty() const { return m_entries.empty(); }

    // Find by pass id; nullptr if absent. (const + mutable.)
    const LedgerEntry* find(const std::string& id) const;
    LedgerEntry*       find(const std::string& id);

    // Does the entry's recorded body_hash equal `hash`? False if the id is
    // unknown. This is the "is the file back the file I sent?" check import runs
    // after recomputing the returned body's hash. An empty stored hash never
    // matches (nothing to compare against).
    bool body_matches(const std::string& id, const std::string& hash) const;

    // ── mutations ──────────────────────────────────────────────────────────────
    // Record an outbound pass. Ids are unique: if `e.id` already exists the entry
    // is REPLACED in place (re-issuing the same pass overwrites its record);
    // otherwise appended. Returns a reference to the stored entry.
    LedgerEntry& record_sent(LedgerEntry e);

    // Flip a pass to Returned and stamp when + how many annotations came back.
    // Returns false (and changes nothing) if the id is unknown.
    bool mark_returned(const std::string& id, const std::string& returned_at,
                       int annotation_count);

    // ── persistence (pure JSON; Folio writes this into the .folio bundle) ──────
    json        to_json() const;
    void        from_json(const json& j);
    std::string dump(int indent = 2) const;              // -> string for the bundle
    static InterchangeLedger parse(const std::string& text);  // <- bundle string

private:
    std::vector<LedgerEntry> m_entries;
};

}  // namespace Folio
