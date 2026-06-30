#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineClock.hpp — story-time duration formatting for the world-clock axis
// (DESIGN_timeline.md §9.14, build-order step 1). Pure, GTK-free, header-only.
//
// The world-clock axis labels the elapsed story-time between scenes — "2 hours
// later", "6 weeks later", "↩ 2 years earlier" (§9.14.2). This unit turns a
// duration into that label, and is the one genuinely testable pure piece of the
// feature (many unit-boundary cases — a real test, not theatre, §9.14.6 step 1).
//
// TWO LABEL SOURCES, ONE FORMATTER (the §9.14.2 data decision made concrete):
//   • AUTHORED gap — the writer typed "6 weeks" onto a boundary, stored as
//     (count, unit). format_count_unit(6, Week) -> "6 weeks". No unit selection;
//     the authored unit IS the label, so it round-trips exactly (never re-derived
//     into a coarser unit and lost).
//   • DERIVED gap — computed from two absolute marks as a raw seconds delta.
//     duration_label(seconds) PICKS the coarsest unit ≥ 1 and formats it. Lossy
//     by nature (6 weeks of seconds reads back as "1 month"), which is why an
//     authored gap stores its unit rather than only its seconds.
//
// gap_phrase(signed_seconds) adds the §9.14.2 direction: 0 -> "same time",
// forward -> "<label> later", backward (a flashback) -> "<label> earlier". The
// renderer may instead take the bare duration_label and supply its own arrow
// glyph (→ / ↩) — both are available so presentation isn't baked in here.
//
// NOMINAL months/years (month = 30 days, year = 365 days). Story-time is the
// story's own clock (§9.14.4 "world-clock, not calendar"), so a calendar-exact
// month/year would be false precision; nominal is the honest choice and keeps the
// arithmetic integer-exact. OPEN (§9.14.2): compound labels ("2 years 3 months")
// vs the single coarsest unit shipped here; fuzzy rendering ("~weeks") once a
// mark can carry imprecision. Header-only inline: small enough to not earn a
// .cpp + a CMakeLists entry (the TimelineFocus precedent).
// ─────────────────────────────────────────────────────────────────────────────

#include <string>

namespace Folio {

// The unit set from the design (§9.14.2), coarsest → finest. The enum order is
// NOT the selection order; duration_label walks kCoarseToFine below.
enum class DurationUnit { Second, Minute, Hour, Day, Week, Month, Year };

// Nominal seconds per unit. Month/year are nominal (30 / 365 days) on purpose —
// see the header note.
inline long long unit_seconds(DurationUnit u) {
  switch (u) {
    case DurationUnit::Second: return 1LL;
    case DurationUnit::Minute: return 60LL;
    case DurationUnit::Hour:   return 3600LL;
    case DurationUnit::Day:    return 86400LL;
    case DurationUnit::Week:   return 604800LL;
    case DurationUnit::Month:  return 2592000LL;     // 30 days
    case DurationUnit::Year:   return 31536000LL;    // 365 days
  }
  return 1LL;
}

inline const char* unit_name(DurationUnit u, bool plural) {
  switch (u) {
    case DurationUnit::Second: return plural ? "seconds" : "second";
    case DurationUnit::Minute: return plural ? "minutes" : "minute";
    case DurationUnit::Hour:   return plural ? "hours"   : "hour";
    case DurationUnit::Day:    return plural ? "days"    : "day";
    case DurationUnit::Week:   return plural ? "weeks"   : "week";
    case DurationUnit::Month:  return plural ? "months"  : "month";
    case DurationUnit::Year:   return plural ? "years"   : "year";
  }
  return "";
}

// AUTHORED label: a stored (count, unit) rendered verbatim, plural-aware. Singular
// only at exactly 1 ("1 day"); everything else pluralises ("0 days", "6 weeks").
inline std::string format_count_unit(long long count, DurationUnit unit) {
  return std::to_string(count) + " " + unit_name(unit, count != 1);
}

// DERIVED label: a raw (possibly signed) seconds delta → its magnitude in the
// coarsest unit that fits, rounded to the nearest whole of that unit. 0 -> the
// explicit zero magnitude (gap_phrase turns 0 into "same time"). Sign is ignored
// here — direction is gap_phrase's job.

// The coarsest unit that fits |seconds| (≥ 1 of it), with the nearest-whole count
// in that unit. The shared core of duration_label / duration_abbrev, and what the
// broken-axis ruler (step 4) reads to size each gap's ticks and to decide a
// collapse-block. 0 -> {0, Second}. Sign ignored.
struct UnitCount {
  long long    count = 0;
  DurationUnit unit  = DurationUnit::Second;
};

inline UnitCount coarsest_unit(long long seconds) {
  long long s = seconds < 0 ? -seconds : seconds;
  if (s == 0) return UnitCount{0, DurationUnit::Second};
  static const DurationUnit kCoarseToFine[] = {
      DurationUnit::Year,  DurationUnit::Month, DurationUnit::Week,
      DurationUnit::Day,   DurationUnit::Hour,  DurationUnit::Minute,
      DurationUnit::Second};
  for (DurationUnit u : kCoarseToFine) {
    const long long us = unit_seconds(u);
    if (s >= us) return UnitCount{(s + us / 2) / us, u};   // nearest whole (us/2 == 0 for Second)
  }
  return UnitCount{0, DurationUnit::Second};   // unreachable (Second fits everything ≥ 1)
}

inline std::string duration_label(long long seconds) {
  const UnitCount uc = coarsest_unit(seconds);
  return format_count_unit(uc.count, uc.unit);   // {0, Second} -> "0 seconds"
}

// COMPACT magnitude token for a dense axis ("2y", "3mo", "6w", "5h", "4d",
// "30min", "45s") — same coarsest-unit selection as duration_label, sign ignored
// (the renderer encodes direction by colour). For the on-spine gap chip where the
// 16px gutter has no room for the worded phrase; the peek panel carries the full
// "X later/earlier" (§9.14.2). Month is "mo" so it never collides with minute.
inline std::string duration_abbrev(long long seconds) {
  const UnitCount uc = coarsest_unit(seconds);
  if (uc.count == 0) return "0s";
  const char* tok = "";
  switch (uc.unit) {
    case DurationUnit::Year:   tok = "y";   break;
    case DurationUnit::Month:  tok = "mo";  break;
    case DurationUnit::Week:   tok = "w";   break;
    case DurationUnit::Day:    tok = "d";   break;
    case DurationUnit::Hour:   tok = "h";   break;
    case DurationUnit::Minute: tok = "min"; break;
    case DurationUnit::Second: tok = "s";   break;
  }
  return std::to_string(uc.count) + tok;
}

// DERIVED phrase with direction (§9.14.2). 0 -> "same time"; forward gap ->
// "<label> later"; backward gap (flashback) -> "<label> earlier".
inline std::string gap_phrase(long long signed_seconds) {
  if (signed_seconds == 0) return "same time";
  const std::string mag = duration_label(signed_seconds);
  return mag + (signed_seconds > 0 ? " later" : " earlier");
}

// ─── Boundary gaps on the told-order spine (step 2) ──────────────────────────
// A scene's OPTIONAL absolute story-time coordinate (Option B, the model
// decision: reorder-safe, feeds the chronological sort directly; §9.14.1/.4). The
// GTK side fills this from BinderNode's nullable field; the pure layer never sees
// BinderNode. `known == false` => an undated scene (ordinal-only, §9.14.1).
struct StoryTime {
  bool      known   = false;
  long long seconds = 0;   // from the project epoch (scene 1 = 0); may be < 0
};

// What the gap pill between two consecutive told-order scenes shows. `known` only
// when BOTH endpoints are dated — an undated scene on either side means there is
// nothing to subtract, so no pill (§9.14.1). `delta` is signed (negative = the
// next told scene happens EARLIER, i.e. a flashback boundary).
struct SpineGap {
  bool        known  = false;
  long long   delta  = 0;
  std::string phrase;       // gap_phrase(delta): "same time" / "X later" / "X earlier"
};

inline SpineGap spine_gap(StoryTime prev, StoryTime next) {
  if (!prev.known || !next.known) return SpineGap{};
  const long long d = next.seconds - prev.seconds;
  return SpineGap{true, d, gap_phrase(d)};
}

// AUTHORING (the §9.14.2 relative front door over the Option-B store): the writer
// types a relative gap against the previous scene; this returns THIS scene's
// absolute coordinate. dir >= 0 => "later" (forward), dir < 0 => "earlier"
// (a flashback). The stored truth stays absolute, so a later reorder cannot
// invalidate it — only the displayed phrase recomputes from whoever is adjacent.
inline long long apply_relative_gap(long long prev_seconds, long long count,
                                    DurationUnit unit, int dir) {
  const long long mag = count * unit_seconds(unit);
  return prev_seconds + (dir < 0 ? -mag : mag);
}

}  // namespace Folio
