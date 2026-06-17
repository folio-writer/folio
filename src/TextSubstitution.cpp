// ─────────────────────────────────────────────────────────────────────────────
// Folio — TextSubstitution.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "TextSubstitution.hpp"
#include <glibmm/ustring.h>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / connect / disconnect
// ─────────────────────────────────────────────────────────────────────────────

TextSubstitution::TextSubstitution(Glib::RefPtr<Gtk::TextBuffer> buffer,
                                   const FolioPrefs& prefs)
    : m_buffer(std::move(buffer)), m_prefs(prefs) {}

void TextSubstitution::connect() {
    if (m_connected) return;
    m_conn = m_buffer->signal_insert().connect(
        [this](const Gtk::TextBuffer::iterator& pos,
               const Glib::ustring& text,
               int len) {
            on_insert(pos, text, len);
        }, false); // false = connect before default handler
    m_connected = true;
}

void TextSubstitution::disconnect() {
    m_conn.disconnect();
    m_connected = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

Glib::ustring TextSubstitution::chars_before(
        const Gtk::TextBuffer::iterator& pos, int n) const {
    auto it = pos;
    Glib::ustring out;
    for (int i = 0; i < n; ++i) {
        if (!it.backward_char()) break;
        // prepend the character
        Glib::ustring ch(1, it.get_char());
        out = ch + out;
    }
    return out;
}

bool TextSubstitution::is_word_boundary(gunichar c) {
    return c == ' ' || c == '\n' || c == '\t' ||
           c == '.' || c == ',' || c == '!' || c == '?' ||
           c == ';' || c == ':' || c == '(' || c == ')' ||
           c == '[' || c == ']' || c == '{' || c == '}' ||
           c == '>' ||   // closes <special> shortcuts
           c == '"' || c == '\'' || c == 0x201C || c == 0x201D ||
           c == 0x2018 || c == 0x2019;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dispatch
// ─────────────────────────────────────────────────────────────────────────────

void TextSubstitution::on_insert(const Gtk::TextBuffer::iterator& pos,
                                 const Glib::ustring& text,
                                 int /*len*/) {
    // Re-entrancy guard — our own replacements trigger signal_insert_text again
    if (m_inhibit) return;

    // Only act on single-character insertions from the keyboard.
    // Multi-character inserts are paste/load operations — leave them alone.
    if (text.size() != 1) return;

    m_inhibit = true;

    bool handled = false;
    if (!handled && m_prefs.sub_smart_quotes) handled = try_smart_quote(pos, text);
    if (!handled && m_prefs.sub_em_dash)      handled = try_em_dash    (pos, text);
    if (!handled && m_prefs.sub_ellipsis)     handled = try_ellipsis   (pos, text);
    if (!handled && m_prefs.sub_autocorrect)  handled = try_autocorrect(pos, text);

    m_inhibit = false;

    // If we handled the insertion ourselves we need to stop the default insert.
    // GTK's signal_insert_text does not support returning false to suppress —
    // instead we let it insert the original character, then immediately delete
    // it and replace with our substitution.  This is done inside each handler.
}

// ─────────────────────────────────────────────────────────────────────────────
// Smart quotes
// Typed char   Context                     Replacement
// "            start of buffer/after space → \u201c (left double)
// "            after any other character   → \u201d (right double)
// '            start of buffer/after space → \u2018 (left single)
// '            after any other character   → \u2019 (right single)
// ─────────────────────────────────────────────────────────────────────────────

bool TextSubstitution::try_smart_quote(const Gtk::TextBuffer::iterator& pos,
                                       const Glib::ustring& text) {
    gunichar typed = text[0];
    if (typed != '"' && typed != '\'') return false;

    // Determine context: what's immediately before the insertion point?
    auto prev = pos;
    bool at_start = !prev.backward_char();
    gunichar before = at_start ? 0 : prev.get_char();
    bool open_context = at_start || before == ' ' || before == '\n' ||
                        before == '\t' || before == '(' || before == '[' ||
                        before == 0x201C || before == 0x2018;

    gunichar replacement;
    if (typed == '"')
        replacement = open_context ? 0x201C : 0x201D; // " or "
    else
        replacement = open_context ? 0x2018 : 0x2019; // ' or '

    // Let GTK insert the original character, then replace it immediately
    Glib::signal_idle().connect_once([this, replacement]() {
        if (m_inhibit) return;
        m_inhibit = true;

        // The cursor is now just after the character we want to replace
        auto insert_mark = m_buffer->get_insert();
        auto end_it = m_buffer->get_iter_at_mark(insert_mark);
        auto start_it = end_it;
        if (start_it.backward_char()) {
            // Collect paragraph-level tags (li:/ri:) from the character
            // AFTER the replacement position — so the new char inherits them.
            // We look both before and after: the char being replaced may be
            // at the paragraph start (no left neighbour with the tag) so we
            // also check the char to the right if available.
            std::vector<Glib::RefPtr<Gtk::TextTag>> para_tags;
            auto collect = [&](Gtk::TextBuffer::iterator it) {
                for (auto& tag : it.get_tags()) {
                    const std::string& n = tag->property_name().get_value();
                    if (n.size() > 3 && (n.substr(0,3) == "li:" || n.substr(0,3) == "ri:")) {
                        bool already = false;
                        for (auto& t : para_tags) if (t == tag) { already = true; break; }
                        if (!already) para_tags.push_back(tag);
                    }
                }
            };
            collect(start_it);          // tags on the char being replaced
            auto after = end_it;        // char after (next paragraph char)
            collect(after);

            m_buffer->erase(start_it, end_it);
            auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
            m_buffer->insert(ins, Glib::ustring(1, replacement));

            // Re-apply paragraph tags to the newly inserted character
            if (!para_tags.empty()) {
                auto new_end = m_buffer->get_iter_at_mark(m_buffer->get_insert());
                auto new_start = new_end;
                new_start.backward_char();
                for (auto& tag : para_tags)
                    m_buffer->apply_tag(tag, new_start, new_end);
            }
        }

        m_inhibit = false;
    });

    return false; // let the original insert proceed; idle will replace it
}

// ─────────────────────────────────────────────────────────────────────────────
// Em dash  (-- → —)
// Fires when the user types the second '-' and the preceding character is '-'
// ─────────────────────────────────────────────────────────────────────────────

bool TextSubstitution::try_em_dash(const Gtk::TextBuffer::iterator& pos,
                                   const Glib::ustring& text) {
    if (text[0] != '-') return false;
    Glib::ustring prev = chars_before(pos, 1);
    if (prev.empty() || prev[0] != '-') return false;

    Glib::signal_idle().connect_once([this]() {
        if (m_inhibit) return;
        m_inhibit = true;

        // Cursor is after the second '-'; delete both and insert em dash
        auto insert_mark = m_buffer->get_insert();
        auto end_it = m_buffer->get_iter_at_mark(insert_mark);
        auto start_it = end_it;
        // Step back 2 characters (the original '-' and the just-inserted '-')
        if (start_it.backward_chars(2)) {
            m_buffer->erase(start_it, end_it);
            auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
            m_buffer->insert(ins, Glib::ustring(1, (gunichar)0x2014)); // —
        }

        m_inhibit = false;
    });

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ellipsis  (... → …)
// Fires when the user types the third '.' and the two preceding chars are '..'
// ─────────────────────────────────────────────────────────────────────────────

bool TextSubstitution::try_ellipsis(const Gtk::TextBuffer::iterator& pos,
                                    const Glib::ustring& text) {
    if (text[0] != '.') return false;
    Glib::ustring prev = chars_before(pos, 2);
    if (prev.size() < 2 || prev[0] != '.' || prev[1] != '.') return false;

    Glib::signal_idle().connect_once([this]() {
        if (m_inhibit) return;
        m_inhibit = true;

        auto insert_mark = m_buffer->get_insert();
        auto end_it = m_buffer->get_iter_at_mark(insert_mark);
        auto start_it = end_it;
        if (start_it.backward_chars(3)) { // delete all three dots
            m_buffer->erase(start_it, end_it);
            auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
            m_buffer->insert(ins, Glib::ustring(1, (gunichar)0x2026)); // …
        }

        m_inhibit = false;
    });

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Autocorrect
// Fires on space, newline, or punctuation after a word that matches a pair.
// We look back to find the previous word boundary, extract the typed word,
// and check it against all pairs.
// ─────────────────────────────────────────────────────────────────────────────

bool TextSubstitution::try_autocorrect(const Gtk::TextBuffer::iterator& pos,
                                       const Glib::ustring& text) {
    // Only trigger on word-ending characters
    if (!is_word_boundary(text[0])) return false;
    if (m_prefs.autocorrect_pairs.empty()) return false;

    // Walk backwards from pos to find the start of the current word.
    // For self-terminating shortcuts like <nbsp> the trigger char '>' is part
    // of the pattern — we collect it into the word by walking back until we
    // hit a boundary OTHER than the trigger char, or a '<' opener.
    auto it = pos;
    Glib::ustring word;
    gunichar trigger = text[0];

    // First: check if this could be a self-contained <...> or (xx) pattern —
    // i.e. the trigger char '>' or ')' closes the pattern.  Collect everything
    // back to and including the matching opener, then append the trigger.
    if (trigger == '>' || trigger == ')') {
        gunichar opener = (trigger == '>') ? '<' : '(';
        auto scan = pos;
        Glib::ustring candidate;
        bool found_opener = false;
        // Walk back up to 16 chars looking for the opener
        for (int i = 0; i < 16; ++i) {
            if (!scan.backward_char()) break;
            gunichar c = scan.get_char();
            Glib::ustring ch(1, c);
            candidate = ch + candidate;
            if (c == opener) { found_opener = true; break; }
            // Bail if we hit another boundary
            if (is_word_boundary(c) && c != opener) break;
        }
        if (found_opener) {
            // Full pattern = candidate + trigger char
            Glib::ustring full_pattern = candidate + Glib::ustring(1, trigger);
            for (const auto& pair : m_prefs.autocorrect_pairs) {
                if (pair.first.empty()) continue;
                if (Glib::ustring(pair.first) == full_pattern) {
                    const std::string replacement = pair.second;
                    const int pat_len = (int)full_pattern.size();
                    Glib::signal_idle().connect_once([this, pat_len, replacement]() {
                        if (m_inhibit) return;
                        m_inhibit = true;
                        auto end_it = m_buffer->get_iter_at_mark(m_buffer->get_insert());
                        auto start_it = end_it;
                        // Delete the entire pattern including trigger char
                        if (start_it.backward_chars(pat_len)) {
                            m_buffer->erase(start_it, end_it);
                            auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
                            m_buffer->insert(ins, Glib::ustring(replacement));
                        }
                        m_inhibit = false;
                    });
                    return false;
                }
            }
        }
    }

    // Standard boundary-triggered replacement (space, period, etc.)
    // Walk backwards to find the word before the trigger.
    it = pos;
    word.clear();
    while (it.backward_char()) {
        gunichar c = it.get_char();
        if (is_word_boundary(c)) {
            it.forward_char();
            break;
        }
        Glib::ustring ch(1, c);
        word = ch + word;
    }

    if (word.empty()) return false;

    for (const auto& pair : m_prefs.autocorrect_pairs) {
        if (pair.first.empty()) continue;
        if (Glib::ustring(pair.first) == word) {
            const std::string replacement = pair.second;
            const int word_len = (int)word.size();

            Glib::signal_idle().connect_once([this, word_len, replacement]() {
                if (m_inhibit) return;
                m_inhibit = true;

                auto insert_mark = m_buffer->get_insert();
                auto end_it = m_buffer->get_iter_at_mark(insert_mark);
                auto start_it = end_it;
                start_it.backward_chars(1 + word_len);
                auto word_end = start_it;
                word_end.forward_chars(word_len);

                m_buffer->erase(start_it, word_end);
                auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
                m_buffer->insert(ins, Glib::ustring(replacement));

                m_inhibit = false;
            });

            return false;
        }
    }

    return false;
}

} // namespace Folio
