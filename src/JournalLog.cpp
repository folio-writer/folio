// ─────────────────────────────────────────────────────────────────────────────
// JournalLog.cpp — deck-log lifecycle (see JournalLog.hpp). Pure, std-lib only.
// ─────────────────────────────────────────────────────────────────────────────

#include "JournalLog.hpp"

#include <nlohmann/json.hpp>

namespace Folio {

namespace {
using json = nlohmann::json;
constexpr int kSchema = 1;

bool is_blank(const std::string& s) {
  for (char c : s)
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      return false;
  return true;
}
} // namespace

JournalLog::JournalLog() {
  m_entries.emplace_back(); // one waiting, uncommitted draft
}

void JournalLog::commit_draft(const std::string& now_iso) {
  JLogEntry& d = m_entries.back();
  if (!d.committed())          // idempotent: the start time is set once, forever
    d.iso_dt = now_iso;
}

bool JournalLog::accept_draft() {
  JLogEntry& d = m_entries.back();
  if (!d.committed())          // nothing to accept; never lock an empty record
    return false;
  d.accepted = true;
  m_entries.emplace_back();    // a fresh draft opens beneath the new record
  return true;
}

void JournalLog::soft_delete(std::size_t i) {
  if (i >= m_entries.size())
    return;
  if (i == draft_index()) {
    m_entries.back() = JLogEntry{}; // clear the draft → clock restarts
    return;
  }
  m_entries[i].deleted = true;      // accepted record: retained, hidden
}

std::vector<std::size_t> JournalLog::accepted_view() const {
  std::vector<std::size_t> out;
  // Walk backward so the working view is newest-first; the trailing draft and
  // any soft-deleted records are excluded.
  for (std::size_t k = m_entries.size(); k-- > 0;) {
    const JLogEntry& e = m_entries[k];
    if (e.accepted && !e.deleted)
      out.push_back(k);
  }
  return out;
}

std::vector<std::size_t> JournalLog::export_view(bool include_deleted) const {
  std::vector<std::size_t> out;
  for (std::size_t k = 0; k < m_entries.size(); ++k) {
    const JLogEntry& e = m_entries[k];
    if (!e.committed())            // the uncommitted draft is never exported
      continue;
    if (e.deleted && !include_deleted)
      continue;
    out.push_back(k);
  }
  return out;
}

std::string JournalLog::to_json() const {
  json arr = json::array();
  for (const JLogEntry& e : m_entries) {
    if (!e.committed())            // skip the empty waiting draft; rebuilt on load
      continue;
    arr.push_back({{"dt", e.iso_dt},
                   {"text", e.text},
                   {"tags", e.tags},
                   {"accent", e.accent},
                   {"mood", e.mood},
                   {"accepted", e.accepted},
                   {"deleted", e.deleted}});
  }
  json doc{{"v", kSchema}, {"entries", std::move(arr)}};
  return doc.dump();
}

JournalLog JournalLog::from_json(const std::string& body) {
  JournalLog log;
  log.m_entries.clear(); // we rebuild from scratch, then guarantee a trailing draft

  if (is_blank(body)) {
    log.m_entries.emplace_back(); // a fresh waiting draft
    return log;
  }

  json doc;
  bool parsed = false;
  try {
    doc = json::parse(body);
    parsed = doc.is_object() && doc.contains("entries") && doc["entries"].is_array();
  } catch (...) {
    parsed = false;
  }

  if (!parsed) {
    // Not our JSON (e.g. a legacy prose body): keep the writing, losslessly, as
    // one accepted record rather than discard it. The stamp is unknown, so it is
    // left empty-but-marked-accepted via a sentinel date so committed() holds.
    JLogEntry legacy;
    legacy.iso_dt = "0000-00-00T00:00:00"; // unknown origin; sorts before all real
    legacy.text = body;
    legacy.accepted = true;
    log.m_entries.push_back(std::move(legacy));
    log.m_entries.emplace_back(); // fresh draft
    return log;
  }

  for (const auto& je : doc["entries"]) {
    JLogEntry e;
    e.iso_dt = je.value("dt", std::string{});
    e.text = je.value("text", std::string{});
    if (je.contains("tags") && je["tags"].is_array())
      for (const auto& t : je["tags"])
        e.tags.push_back(t.get<std::string>());
    e.accent = je.value("accent", std::string{});
    e.mood = je.value("mood", std::string{});
    e.accepted = je.value("accepted", false);
    e.deleted = je.value("deleted", false);
    if (e.committed())             // ignore any stray empty rows
      log.m_entries.push_back(std::move(e));
  }

  // Class invariant: back() is always the live draft. If the stored tail was a
  // committed-but-unaccepted draft it is already the draft; otherwise open a
  // fresh one (covers an empty log and a log whose last row was accepted).
  if (log.m_entries.empty() || log.m_entries.back().accepted)
    log.m_entries.emplace_back();
  return log;
}

} // namespace Folio
