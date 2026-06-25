// Journal.cpp — pure implementations for Journal.hpp. No GTK, no I/O; only the
// span structs in, derived answers out. See header for the extractor invariant
// (entries append-ordered, partitioning the buffer).

#include "Journal.hpp"

#include <algorithm>

namespace Folio {

void j_split_dt(const std::string &payload, std::string &iso,
                std::string &accent, std::string &mood) {
  iso = payload;
  accent.clear();
  mood.clear();
  size_t p1 = payload.find('|');
  if (p1 == std::string::npos)
    return; // bare iso, no sidecar
  iso = payload.substr(0, p1);
  size_t p2 = payload.find('|', p1 + 1);
  if (p2 == std::string::npos) {
    accent = payload.substr(p1 + 1); // "<iso>|<accent>" (mood omitted)
    return;
  }
  accent = payload.substr(p1 + 1, p2 - (p1 + 1));
  mood = payload.substr(p2 + 1); // rest is mood (a single emoji)
}

std::string j_make_dt(const std::string &iso, const std::string &accent,
                      const std::string &mood) {
  if (accent.empty() && mood.empty())
    return iso; // no sidecar
  return iso + "|" + accent + "|" + mood;
}

std::string j_date_key(const std::string &iso_dt) {
  return iso_dt.size() >= 10 ? iso_dt.substr(0, 10) : std::string();
}

int j_days_in_month(int year, int month) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12)
    return 30;
  if (month == 2) {
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return d[month - 1];
}

int j_weekday(int year, int month, int day) {
  // Sakamoto's algorithm. 0=Sunday .. 6=Saturday.
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 1 || month > 12)
    return 0;
  int y = year;
  if (month < 3)
    y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

bool j_year_month(const std::string &iso_dt, int &year, int &month) {
  if (iso_dt.size() < 7 || iso_dt[4] != '-')
    return false;
  try {
    year = std::stoi(iso_dt.substr(0, 4));
    month = std::stoi(iso_dt.substr(5, 2));
  } catch (...) {
    return false;
  }
  return month >= 1 && month <= 12;
}

std::map<std::string, int> j_dates_with_entries(const std::vector<JEntry> &es) {
  std::map<std::string, int> out;
  for (const auto &e : es) {
    std::string k = j_date_key(e.iso_dt);
    if (!k.empty())
      ++out[k];
  }
  return out;
}

int j_first_entry_on(const std::vector<JEntry> &es,
                     const std::string &date_key) {
  // Append order => the first matching entry is the earliest on that day.
  for (size_t i = 0; i < es.size(); ++i)
    if (j_date_key(es[i].iso_dt) == date_key)
      return (int)i;
  return -1;
}

int j_entry_at(const std::vector<JEntry> &es, int caret_offset) {
  // Entries partition the buffer in order, so the active entry is the one with
  // the greatest start <= caret_offset. -1 if the caret precedes the first.
  int found = -1;
  int best_start = -1;
  for (size_t i = 0; i < es.size(); ++i) {
    if (es[i].start <= caret_offset && es[i].start >= best_start) {
      best_start = es[i].start;
      found = (int)i;
    }
  }
  return found;
}

int j_last_entry(const std::vector<JEntry> &es) {
  if (es.empty())
    return -1;
  int best = 0;
  for (size_t i = 1; i < es.size(); ++i)
    // >= so a tie resolves to the later index (the more recently appended).
    if (es[i].iso_dt >= es[best].iso_dt)
      best = (int)i;
  return best;
}

int j_resume_offset(const std::vector<JEntry> &es) {
  int i = j_last_entry(es);
  return i < 0 ? 0 : es[(size_t)i].end;
}

void j_assign_concepts(const std::vector<JEntry> &es, std::vector<JConcept> &cs) {
  for (auto &c : cs)
    c.entry_index = j_entry_at(es, c.start);
}

std::map<std::string, std::vector<int>>
j_concept_index(const std::vector<JConcept> &cs) {
  std::map<std::string, std::vector<int>> out;
  for (const auto &c : cs) {
    if (c.entry_index < 0)
      continue;
    out[c.label].push_back(c.entry_index);
  }
  for (auto &kv : out) {
    auto &v = kv.second;
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  }
  return out;
}

// ── Title + flair ──

static const char *kStar = "\xE2\x98\x85"; // U+2605 BLACK STAR

static std::string j_trim(const std::string &s) {
  size_t a = 0, b = s.size();
  auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
  while (a < b && sp(s[a]))
    ++a;
  while (b > a && sp(s[b - 1]))
    --b;
  return s.substr(a, b - a);
}

std::string j_first_line(const std::string &text) {
  size_t i = 0;
  while (i < text.size()) {
    size_t nl = text.find('\n', i);
    std::string line =
        j_trim(text.substr(i, nl == std::string::npos ? std::string::npos : nl - i));
    if (!line.empty())
      return line;
    if (nl == std::string::npos)
      break;
    i = nl + 1;
  }
  return std::string();
}

bool j_starred(const std::string &title) {
  std::string t = j_trim(title);
  return t.compare(0, 3, kStar) == 0;
}

std::string j_strip_star(const std::string &title) {
  std::string t = j_trim(title);
  if (t.compare(0, 3, kStar) == 0)
    return j_trim(t.substr(3));
  return t;
}

std::string j_entry_title(const JEntry &e) {
  return j_strip_star(j_first_line(e.text));
}

bool j_entry_starred(const JEntry &e) { return j_starred(j_first_line(e.text)); }

// ── Filter ──

static std::string j_lower(const std::string &s) {
  std::string o = s;
  for (auto &c : o)
    if (c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
  return o;
}

std::vector<int> j_filter(const std::vector<JEntry> &entries,
                          const std::vector<JConcept> &concepts,
                          const std::string &query,
                          const std::vector<std::string> &tags,
                          bool starred_only) {
  // Per-entry tag set from the assigned concepts.
  std::map<int, std::vector<std::string>> tagset;
  for (const auto &c : concepts)
    if (c.entry_index >= 0)
      tagset[c.entry_index].push_back(c.label);

  std::string q = j_lower(j_trim(query));
  std::vector<int> out;
  for (size_t i = 0; i < entries.size(); ++i) {
    const JEntry &e = entries[i];

    if (starred_only && !j_entry_starred(e))
      continue;

    if (!q.empty() && j_lower(e.text).find(q) == std::string::npos)
      continue;

    if (!tags.empty()) {
      const auto &have = tagset[(int)i];
      bool all = true;
      for (const auto &t : tags) {
        bool found = false;
        for (const auto &h : have)
          if (h == t) {
            found = true;
            break;
          }
        if (!found) {
          all = false;
          break;
        }
      }
      if (!all)
        continue;
    }

    out.push_back((int)i);
  }
  return out;
}

} // namespace Folio
