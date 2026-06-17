// ─────────────────────────────────────────────────────────────────────────────
// Folio — SpellCheckHighlighter.hpp
// Watches a Gtk::TextBuffer, checks words against SpellChecker after a short
// idle delay, and applies/removes the spell-error tag to mark misspellings.
//
// Appearance (color, underline style, background tint) is read directly from
// FolioPrefs each time refresh_appearance() is called, so changes in the
// Editing preferences tab take effect immediately.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "FolioPrefs.hpp"
#include "SpellChecker.hpp"
#include <gtkmm/textbuffer.h>
#include <gtkmm/texttag.h>
#include <sigc++/connection.h>
#include <string>

namespace Folio {

class SpellCheckHighlighter {
public:
    SpellCheckHighlighter(Glib::RefPtr<Gtk::TextBuffer> buffer,
                          const FolioPrefs& prefs);
    ~SpellCheckHighlighter();

    // Load (or reload) the dictionary for the given language tag.
    // Pass "" to use the system locale.  Returns true on success.
    bool load_language(const std::string& lang_tag);

    // Connect the buffer-changed signal and run an initial check.
    void connect();

    // Disconnect — call before loading a new document node.
    void disconnect();

    bool is_connected() const { return m_connected; }

    // Re-read appearance settings from prefs and rebuild the error tag.
    // Call after the Editing prefs page is applied.
    void refresh_appearance();

    // Remove all error highlights — call when spell check is disabled.
    void clear_highlights();

    // Run a full check of the entire buffer immediately (e.g. after load).
    void check_all();

    // Per-word session ignore (forwarded to SpellChecker).
    void ignore_word(const std::string& word);

    // Add to persistent user dictionary.
    void add_to_dict(const std::string& word);

    // Get spelling suggestions for a word.
    std::vector<std::string> get_suggestions(const std::string& word) const;

private:
    // Called on buffer changed — queues a debounced recheck.
    void on_buffer_changed();

    // Tokenise buffer text into words and check each one.
    void do_check();

    // Helper — parse a hex color string "#rrggbb" into Gdk::RGBA.
    static Gdk::RGBA hex_to_rgba(const std::string& hex);

    Glib::RefPtr<Gtk::TextBuffer>  m_buffer;
    const FolioPrefs&              m_prefs;
    SpellChecker                   m_checker;

    // The single tag applied to all misspelled ranges.
    Glib::RefPtr<Gtk::TextTag>     m_error_tag;

    sigc::connection               m_changed_conn;
    sigc::connection               m_check_idle;
    bool                           m_connected  = false;
    bool                           m_inhibit    = false; // suppress during load
};

} // namespace Folio
