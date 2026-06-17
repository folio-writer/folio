// ─────────────────────────────────────────────────────────────────────────────
// Folio — TextSubstitution.hpp
// Handles real-time text substitutions as the user types:
//   • Smart quotes  (" " → " "   ' ' → ' ')
//   • Em dash       (-- → —)
//   • Ellipsis      (... → …)
//   • Autocorrect   (user-defined word pairs)
//
// Hooked into Gtk::TextBuffer::signal_insert_text so substitutions fire
// before on_text_changed() and the undo stack sees the final text.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "FolioPrefs.hpp"
#include <gtkmm/textbuffer.h>
#include <string>

namespace Folio {

class TextSubstitution {
public:
    // buffer  — the editor's text buffer (held by ref, not owned)
    // prefs   — live prefs reference so substitutions respect current settings
    TextSubstitution(Glib::RefPtr<Gtk::TextBuffer> buffer,
                     const FolioPrefs& prefs);

    // Connect signal_insert_text on the buffer.
    // Call once after the buffer is created.
    void connect();

    // Disconnect — call before loading a new node to suppress substitutions
    // during programmatic insertions.
    void disconnect();

    bool is_connected() const { return m_connected; }

private:
    // Called by signal_insert_text before the text is committed.
    // pos     — iterator at the insertion point
    // text    — the text about to be inserted
    // len     — byte length of text
    void on_insert(const Gtk::TextBuffer::iterator& pos,
                   const Glib::ustring& text,
                   int len);

    // Individual substitution handlers — each returns true if it fired
    // and the default insert should be suppressed (we do the insert ourselves).
    bool try_smart_quote(const Gtk::TextBuffer::iterator& pos,
                         const Glib::ustring& text);
    bool try_em_dash    (const Gtk::TextBuffer::iterator& pos,
                         const Glib::ustring& text);
    bool try_ellipsis   (const Gtk::TextBuffer::iterator& pos,
                         const Glib::ustring& text);
    bool try_autocorrect(const Gtk::TextBuffer::iterator& pos,
                         const Glib::ustring& text);

    // Helper — get the N characters immediately before pos as a ustring
    Glib::ustring chars_before(const Gtk::TextBuffer::iterator& pos, int n) const;

    // Helper — is this character a word boundary (space, newline, punctuation)?
    static bool is_word_boundary(gunichar c);

    Glib::RefPtr<Gtk::TextBuffer> m_buffer;
    const FolioPrefs&             m_prefs;
    sigc::connection              m_conn;
    bool                          m_connected  = false;
    bool                          m_inhibit    = false; // re-entrancy guard
};

} // namespace Folio
