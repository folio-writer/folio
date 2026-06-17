// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorHtmlSerializer.hpp
// Converts between a Gtk::TextBuffer (with Folio's tag conventions) and an
// HTML string.  Extracted from Editor so the serialization logic is testable
// and maintainable independently of the editor UI.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <gtkmm/textbuffer.h>
#include <array>
#include "FolioPrefs.hpp"
#include <gtkmm/texttag.h>
#include <string>

namespace Folio {

class EditorHtmlSerializer {
public:
    // Tag references needed for deserialization — all owned by the buffer's
    // tag table.  Pass the same tag pointers that Editor creates in its ctor.
    struct Tags {
        Glib::RefPtr<Gtk::TextTag> bold;
        Glib::RefPtr<Gtk::TextTag> italic;
        Glib::RefPtr<Gtk::TextTag> underline;
        Glib::RefPtr<Gtk::TextTag> strikethrough;
        Glib::RefPtr<Gtk::TextTag> justify_left;
        Glib::RefPtr<Gtk::TextTag> justify_center;
        Glib::RefPtr<Gtk::TextTag> justify_right;
        Glib::RefPtr<Gtk::TextTag> justify_full;
        // Outline indent level tags — one per level up to MAX_OUTLINE_LEVELS
        std::array<Glib::RefPtr<Gtk::TextTag>, MAX_OUTLINE_LEVELS> ol;
    };

    explicit EditorHtmlSerializer(Glib::RefPtr<Gtk::TextBuffer> buffer,
                                  const Tags& tags);

    // Serialize the buffer contents to an HTML string.
    std::string to_html() const;

    // Deserialize an HTML string into the buffer, applying tags.
    void from_html(const std::string& html);

private:
    Glib::RefPtr<Gtk::TextBuffer> m_buffer;
    Tags m_tags;
};

} // namespace Folio
