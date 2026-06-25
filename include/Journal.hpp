#pragma once
// Journal — pure navigation / index model for the one-file journal.
//
// The journal is a SINGLE prose fragment (the Note-routes-to-prose path): DT-tag
// runs delimit entries, concept: tag runs mark core concepts. The GTK side walks
// the live buffer once to extract these as flat spans (buffer char offsets);
// everything navigational or derived is computed HERE — GTK-free, no buffer, no
// widgets — so it is sandbox-testable. Spans in, answers out: the calendar's
// dated days, the active ("you are here") entry, the resume point, and the
// concept relief (concept -> the entries that mention it).
//
// Invariant the extractor must keep: entries are in append (chronological) order
// and partition the buffer — entry i covers [start, end), end == next entry's
// start, and the last entry's end == buffer end. DT is auto-stamped, so append
// order is chronological order.

#include <map>
#include <string>
#include <vector>

namespace Folio {

// Front-door sentinel: a journal is a Reference whose template_id is this
// reserved marker. Like the Mind Map's, it is NOT a registered built-in (it must
// not resolve to a floor type); unlike the Mind Map it routes to the plain prose
// editor, not a custom surface — a journal IS prose.
inline constexpr const char *kJournalTemplateId = "journal";

// One entry, opened by a DT tag. iso_dt is the auto-stamped start
// "YYYY-MM-DDTHH:MM:SS"; [start, end) are buffer char offsets.
struct JEntry {
  std::string iso_dt;
  int start = 0;
  int end = 0;
  // The entry BODY (the prose after the stamp line), filled by the extractor.
  // Title is derived from it (first line); content filtering searches it.
  std::string text{};
  // Flair sidecar (carried on the DT stamp, optional). accent is a short colour
  // token for the card spine; mood is a single emoji. "" = default / none.
  std::string accent{};
  std::string mood{};
};

// A concept: run inside the prose. label is the freeform concept (need not be a
// node yet — promotion to a real object is the membrane, later). [start, end) is
// its buffer span; entry_index is filled by j_assign_concepts (-1 until then).
struct JConcept {
  std::string label;
  int start = 0;
  int end = 0;
  int entry_index = -1;
};

// ── DT stamp payload codec (the one structured carrier) ──
// The dt: tag NAME carries the entry's iso plus the optional 2-field flair
// sidecar, encoded as "<iso>|<accent>|<mood>" (no sidecar -> bare "<iso>").
// '|' is a safe separator: the iso never contains one, accent is a short colour
// token, and mood is a single emoji. The whole payload round-trips verbatim
// through the serializer's data-folio-dt attribute, so this codec is the only
// place that knows the layout.

// Parse a dt: payload (the tag name minus its "dt:" prefix) into its fields.
// iso is everything before the first '|'; accent/mood follow. Missing fields
// come back empty. Inverse of j_make_dt.
void j_split_dt(const std::string &payload, std::string &iso,
                std::string &accent, std::string &mood);

// Build a dt: payload from its fields. Both flair fields empty -> bare iso (no
// sidecar, so a freshly stamped entry's tag name stays "dt:<iso>"). Inverse of
// j_split_dt.
std::string j_make_dt(const std::string &iso, const std::string &accent,
                      const std::string &mood);

// ── Calendar math (for the surface's month grid; pure + testable) ──
// Days in a Gregorian month. month is 1..12; year is full (e.g. 2026).
int j_days_in_month(int year, int month);

// Weekday of a date, 0=Sunday .. 6=Saturday (Sakamoto's algorithm). month 1..12.
int j_weekday(int year, int month, int day);

// Year/month (1..12) parsed from an iso_dt "YYYY-MM-...". Returns false (and
// leaves outputs untouched) if the string is too short / malformed.
bool j_year_month(const std::string &iso_dt, int &year, int &month);

// "YYYY-MM-DD" date key from an iso_dt (first 10 chars; "" if too short).
std::string j_date_key(const std::string &iso_dt);

// date-key -> entry count, for marking days on the calendar.
std::map<std::string, int> j_dates_with_entries(const std::vector<JEntry> &es);

// Index of the first (earliest) entry on a date-key, or -1. Calendar jump target
// is es[idx].start.
int j_first_entry_on(const std::vector<JEntry> &es, const std::string &date_key);

// Index of the entry containing caret_offset (the active entry): the entry with
// the greatest start <= offset. -1 if the caret sits before the first entry.
int j_entry_at(const std::vector<JEntry> &es, int caret_offset);

// Index of the most recent entry (max iso_dt; ties -> later in vector), or -1.
int j_last_entry(const std::vector<JEntry> &es);

// Resume caret offset: end of the most recent entry, or 0 if none.
int j_resume_offset(const std::vector<JEntry> &es);

// Stamp each concept with the entry it falls in (by its start offset).
void j_assign_concepts(const std::vector<JEntry> &es, std::vector<JConcept> &cs);

// The relief: concept label -> sorted, unique entry indices mentioning it.
// Requires j_assign_concepts to have run (skips unassigned / -1).
std::map<std::string, std::vector<int>>
j_concept_index(const std::vector<JConcept> &cs);

// ── Title + flair (first line of the body is the Title; a leading ★ stars it) ──

// First non-empty line of text, trimmed. "" if the text has none.
std::string j_first_line(const std::string &text);

// True if title begins with a ★ (U+2605), after optional leading spaces — the
// inline "starred / favourite" flag (decoration is the data).
bool j_starred(const std::string &title);

// title without a leading ★ and the spaces after it.
std::string j_strip_star(const std::string &title);

// The entry's Title: first body line, star stripped.
std::string j_entry_title(const JEntry &e);

// Whether the entry's Title leads with a ★.
bool j_entry_starred(const JEntry &e);

// ── Filter (content ∩ tags ∩ starred) ──
// Returns the indices of entries passing every ACTIVE filter:
//   query       — case-insensitive substring of the body (incl. Title); "" = off
//   tags         — required concept labels; an entry must carry ALL of them
//                  (intersect). Empty = off. (AND→OR is a one-line change.)
//   starred_only — keep only entries whose Title leads with ★
// concepts must already be assigned (j_assign_concepts) so each carries its
// entry_index. Indices are returned in entry order.
std::vector<int> j_filter(const std::vector<JEntry> &entries,
                          const std::vector<JConcept> &concepts,
                          const std::string &query,
                          const std::vector<std::string> &tags,
                          bool starred_only);

} // namespace Folio
