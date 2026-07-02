// ─────────────────────────────────────────────────────────────────────────────
// Folio — InterchangeLedger.cpp   (see InterchangeLedger.hpp; pure STL + nlohmann)
// ─────────────────────────────────────────────────────────────────────────────
#include "InterchangeLedger.hpp"

#include <algorithm>

namespace Folio {

std::string pass_status_to_str(PassStatus s) {
    switch (s) {
        case PassStatus::Sent:     return "sent";
        case PassStatus::Returned: return "returned";
    }
    return "sent";   // unreachable; keeps -Werror happy
}

PassStatus pass_status_from_str(const std::string& s) {
    if (s == "returned") return PassStatus::Returned;
    return PassStatus::Sent;   // default / legacy
}

// ── LedgerEntry JSON ─────────────────────────────────────────────────────────
json LedgerEntry::to_json() const {
    json inv = json::array();
    for (const auto& sc : inventory)
        inv.push_back({{"iid", sc.iid}, {"title", sc.title}});

    json j = {
        {"id",               id},
        {"recipient",        recipient},
        {"phrase",           phrase},
        {"created_at",       created_at},
        {"body_hash",        body_hash},
        {"inventory",        inv},
        {"status",           pass_status_to_str(status)},
        {"annotation_count", annotation_count},
    };
    // returned_at only exists once the pass is home — omit while Sent for a clean
    // diff (consistent with the annotation `source` omit-when-empty pattern).
    if (!returned_at.empty()) j["returned_at"] = returned_at;
    return j;
}

void LedgerEntry::from_json(const json& j) {
    id               = j.value("id",               "");
    recipient        = j.value("recipient",        "");
    phrase           = j.value("phrase",           "");
    created_at       = j.value("created_at",       "");
    body_hash        = j.value("body_hash",        "");
    status           = pass_status_from_str(j.value("status", "sent"));
    returned_at      = j.value("returned_at",      "");
    annotation_count = j.value("annotation_count", 0);

    inventory.clear();
    if (j.contains("inventory") && j["inventory"].is_array()) {
        for (const auto& s : j["inventory"]) {
            LedgerScene sc;
            sc.iid   = s.value("iid",   "");
            sc.title = s.value("title", "");
            inventory.push_back(std::move(sc));
        }
    }
}

// ── queries ──────────────────────────────────────────────────────────────────
const LedgerEntry* InterchangeLedger::find(const std::string& id) const {
    for (const auto& e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

LedgerEntry* InterchangeLedger::find(const std::string& id) {
    for (auto& e : m_entries)
        if (e.id == id) return &e;
    return nullptr;
}

bool InterchangeLedger::body_matches(const std::string& id,
                                     const std::string& hash) const {
    const LedgerEntry* e = find(id);
    if (!e) return false;
    if (e->body_hash.empty()) return false;   // nothing to compare against
    return e->body_hash == hash;
}

// ── mutations ──────────────────────────────────────────────────────────────────
LedgerEntry& InterchangeLedger::record_sent(LedgerEntry e) {
    if (LedgerEntry* existing = find(e.id)) {
        *existing = std::move(e);   // re-issue overwrites in place
        return *existing;
    }
    m_entries.push_back(std::move(e));
    return m_entries.back();
}

bool InterchangeLedger::mark_returned(const std::string& id,
                                      const std::string& returned_at,
                                      int annotation_count) {
    LedgerEntry* e = find(id);
    if (!e) return false;
    e->status           = PassStatus::Returned;
    e->returned_at      = returned_at;
    e->annotation_count = annotation_count;
    return true;
}

// ── persistence ────────────────────────────────────────────────────────────────
json InterchangeLedger::to_json() const {
    json passes = json::array();
    for (const auto& e : m_entries) passes.push_back(e.to_json());
    return {
        {"folio_interchange_schema", FOLIO_INTERCHANGE_SCHEMA},
        {"passes", passes},
    };
}

void InterchangeLedger::from_json(const json& j) {
    m_entries.clear();
    if (j.contains("passes") && j["passes"].is_array()) {
        for (const auto& p : j["passes"]) {
            LedgerEntry e;
            e.from_json(p);
            m_entries.push_back(std::move(e));
        }
    }
}

std::string InterchangeLedger::dump(int indent) const {
    return to_json().dump(indent);
}

InterchangeLedger InterchangeLedger::parse(const std::string& text) {
    InterchangeLedger led;
    if (text.empty()) return led;                 // no ledger yet -> empty
    led.from_json(json::parse(text));
    return led;
}

}  // namespace Folio
