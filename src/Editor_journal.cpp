// Editor_journal.cpp — dt:/concept: tag styling for loaded prose.
//
// As of s54 the journal is an OWNED instrument: all of its editing — stamping,
// extraction, flair, persistence — lives in JournalSurface over its own buffer
// (the way the Mind Map owns its canvas), and a journal Reference no longer
// borrows the Scene editor's prose view. What remains here are the two run
// stylers Editor::html_to_buffer applies generically when it reloads any body
// that happens to carry dt:/concept: tags, so the visuals match a fresh stamp.

#include <Editor.hpp>
#include <Editor_internal.hpp>

namespace Folio {

void Editor::apply_dt_tag_style(Glib::RefPtr<Gtk::TextTag> tag) {
  if (!tag)
    return;
  // The stamp reads as a quiet header: bold, a touch larger, accent-tinted,
  // with air above so entries separate visually in one continuous file.
  tag->property_weight() = Pango::Weight::BOLD;
  tag->property_scale() = 1.15;
  tag->property_foreground() = detect_dark_mode() ? "#fbbf24" : "#b45309"; // amber
  tag->property_pixels_above_lines() = 18;
}

void Editor::apply_concept_tag_style(Glib::RefPtr<Gtk::TextTag> tag) {
  if (!tag)
    return;
  // A concept is a low-stakes mark, not a link: a soft dotted underline in a
  // muted accent, no colour change to the prose itself.
  tag->property_underline() = Pango::Underline::SINGLE;
  tag->property_underline_rgba() =
      Gdk::RGBA(detect_dark_mode() ? "#a78bfa" : "#7c3aed"); // violet
}

} // namespace Folio
