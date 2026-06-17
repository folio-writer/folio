// ─────────────────────────────────────────────────────────────────────────────
// Folio — SpellCheckHighlighter.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "SpellCheckHighlighter.hpp"
#include "color_utils.hpp"
#include <gdkmm/rgba.h>
#include <glibmm/main.h>
#include <cctype>
#include <cstdio>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

Gdk::RGBA SpellCheckHighlighter::hex_to_rgba(const std::string& hex) {
    // Delegates to the shared parser; fallback is red-ish.
    return Folio::color::from_hex(hex, Folio::color::rgba(0.87, 0.36, 0.45, 1.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

SpellCheckHighlighter::SpellCheckHighlighter(
        Glib::RefPtr<Gtk::TextBuffer> buffer,
        const FolioPrefs& prefs)
    : m_buffer(std::move(buffer)), m_prefs(prefs) {

    // Create the persistent error tag — appearance applied in refresh_appearance()
    m_error_tag = m_buffer->create_tag("spell-error");
    refresh_appearance();
}

SpellCheckHighlighter::~SpellCheckHighlighter() {
    disconnect();
}

// ─────────────────────────────────────────────────────────────────────────────
// load_language
// ─────────────────────────────────────────────────────────────────────────────

bool SpellCheckHighlighter::load_language(const std::string& lang_tag) {
    bool ok = m_checker.load(lang_tag);
    if (ok && m_connected)
        check_all();
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// connect / disconnect
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::connect() {
    if (m_connected) return;
    m_changed_conn = m_buffer->signal_changed().connect(
        [this]() { on_buffer_changed(); });
    m_connected = true;
}

void SpellCheckHighlighter::disconnect() {
    if (m_check_idle.connected())  m_check_idle.disconnect();
    if (m_changed_conn.connected()) m_changed_conn.disconnect();
    m_connected = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh_appearance
// Re-reads all error appearance settings from prefs and updates the tag.
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::refresh_appearance() {
    if (!m_error_tag) return;

    // ── Underline style ───────────────────────────────────────────────────────
    Pango::Underline ul = Pango::Underline::ERROR; // wavy — default
    if      (m_prefs.spell_underline_style == "single") ul = Pango::Underline::SINGLE;
    else if (m_prefs.spell_underline_style == "double") ul = Pango::Underline::DOUBLE;
    m_error_tag->property_underline() = ul;

    // ── Underline color ───────────────────────────────────────────────────────
    Gdk::RGBA ul_color = hex_to_rgba(m_prefs.spell_underline_color);
    m_error_tag->property_underline_rgba() = ul_color;

    // ── Bold underline — use DOUBLE for extra visual weight
    // (Pango has no "thick" underline; DOUBLE is the strongest available emphasis)
    if (m_prefs.spell_underline_bold)
        m_error_tag->property_underline() = Pango::Underline::DOUBLE;

    // ── Background tint ───────────────────────────────────────────────────────
    if (m_prefs.spell_background_tint) {
        Gdk::RGBA bg = hex_to_rgba(m_prefs.spell_background_color);
        m_error_tag->property_background_rgba() = bg;
        m_error_tag->property_background_set() = true;
    } else {
        m_error_tag->property_background_set() = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// clear_highlights
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::clear_highlights() {
    m_buffer->remove_tag(m_error_tag, m_buffer->begin(), m_buffer->end());
}

// ─────────────────────────────────────────────────────────────────────────────
// on_buffer_changed — debounce: schedule a check 400ms after last keystroke
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::on_buffer_changed() {
    if (m_inhibit) return;
    if (!m_prefs.spell_check_enabled) return;
    if (!m_checker.is_loaded()) return;

    // Cancel any pending check and reschedule
    if (m_check_idle.connected()) m_check_idle.disconnect();
    m_check_idle = Glib::signal_timeout().connect(
        [this]() { do_check(); return false; }, 400);
}

// ─────────────────────────────────────────────────────────────────────────────
// check_all — immediate full-buffer check (called after node load)
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::check_all() {
    if (m_check_idle.connected()) m_check_idle.disconnect();
    do_check();
}

// ─────────────────────────────────────────────────────────────────────────────
// do_check — tokenise buffer, check each word, apply/remove error tag
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::do_check() {
    if (!m_prefs.spell_check_enabled || !m_checker.is_loaded()) {
        clear_highlights();
        return;
    }

    m_inhibit = true;

    // Remove all existing error marks
    m_buffer->remove_tag(m_error_tag, m_buffer->begin(), m_buffer->end());

    // Walk the buffer character by character, collecting words and checking them
    auto it = m_buffer->begin();
    auto end = m_buffer->end();

    while (it != end) {
        // Skip non-word characters
        gunichar c = it.get_char();
        if (!g_unichar_isalpha(c) && c != '\'') {
            it.forward_char();
            continue;
        }

        // Found start of a word — collect to end
        auto word_start = it;
        Glib::ustring word;
        while (it != end) {
            c = it.get_char();
            // Allow letters, apostrophes (contractions), hyphens between letters
            if (g_unichar_isalpha(c) || c == '\'' || c == 0x2019 ||
                (c == '-' && !word.empty())) {
                word += Glib::ustring(1, c);
                it.forward_char();
            } else {
                break;
            }
        }
        auto word_end = it;

        // Strip leading/trailing apostrophes/hyphens
        while (!word.empty() && (word[0] == '\'' || word[0] == '-'))
            word.erase(0, 1);
        while (!word.empty() && (word[word.size()-1] == '\'' || word[word.size()-1] == '-'))
            word.erase(word.size()-1, 1);

        // Skip very short words (1 char) and pure numbers
        if (word.size() < 2) continue;

        // Convert to UTF-8 std::string for enchant
        std::string utf8_word = word;

        if (!m_checker.check(utf8_word)) {
            m_buffer->apply_tag(m_error_tag, word_start, word_end);
        }
    }

    m_inhibit = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// ignore_word / add_to_dict
// ─────────────────────────────────────────────────────────────────────────────

void SpellCheckHighlighter::ignore_word(const std::string& word) {
    m_checker.add_to_session(word);
    // Re-check to clear the highlight immediately
    if (m_connected) check_all();
}

void SpellCheckHighlighter::add_to_dict(const std::string& word) {
    m_checker.add_to_user_dict(word);
    if (m_connected) check_all();
}

std::vector<std::string> SpellCheckHighlighter::get_suggestions(
        const std::string& word) const {
    return m_checker.suggest(word);
}

} // namespace Folio
