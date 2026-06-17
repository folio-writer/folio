// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorHtmlSerializer.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include <EditorHtmlSerializer.hpp>
#include <FolioLog.hpp>
#include <gdkmm/rgba.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace Folio {

EditorHtmlSerializer::EditorHtmlSerializer(
    Glib::RefPtr<Gtk::TextBuffer> buffer, const Tags& tags)
    : m_buffer(std::move(buffer)), m_tags(tags) {}

// ─────────────────────────────────────────────────────────────────────────────
// to_html — serialize buffer contents to HTML
// ─────────────────────────────────────────────────────────────────────────────

std::string EditorHtmlSerializer::to_html() const {
    std::string html;
    auto begin = m_buffer->begin(), end = m_buffer->end();

    struct OpenTag {
        std::string name;
        Glib::RefPtr<Gtk::TextTag> tag;
    };
    std::vector<OpenTag> open_stack;

    auto html_escape = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            if      (c == '&')  out += "&amp;";
            else if (c == '<')  out += "&lt;";
            else if (c == '>')  out += "&gt;";
            else if (c == '"')  out += "&quot;";
            else                out += (char)c;
        }
        return out;
    };

    // Helper: determine paragraph wrapper for a buffer position
    auto para_tag = [&](Gtk::TextBuffer::iterator it) -> std::string {
        auto tags = it.get_tags();
        std::string sp_type, ol_level, anchor_id;
        for (auto& t : tags) {
            std::string tn = t->property_name().get_value();
            if (tn.size() > 3 && tn.substr(0, 3) == "sp-")
                sp_type = tn.substr(3);
            else if (tn.size() > 14 && tn.substr(0, 14) == "outline-level-")
                ol_level = tn.substr(14);
            else if (tn.size() > 7 && tn.substr(0, 7) == "anchor:")
                anchor_id = tn.substr(7);
        }
        std::string base;
        if      (!sp_type.empty())  base = "p data-sp=\"" + sp_type + "\"";
        else if (!ol_level.empty()) base = "p data-ol=\"" + ol_level + "\"";
        else                        base = "p";
        if (!anchor_id.empty())
            base += " data-anchor=\"" + anchor_id + "\"";
        return base;
    };

    // Helper: extract closing tag name from para tag string ("p data-ol..." → "p")
    auto para_close = [](const std::string& pt) -> std::string {
        auto sp = pt.find(' ');
        return (sp != std::string::npos) ? pt.substr(0, sp) : pt;
    };

    std::string cur_para = para_tag(begin);
    html += "<" + cur_para + ">";
    auto cur = begin;
    while (cur != end) {
        auto tags_here = cur.get_tags();

        // Close tags no longer active (reverse order)
        for (int i = (int)open_stack.size() - 1; i >= 0; --i) {
            bool still = false;
            for (auto& t : tags_here)
                if (t == open_stack[i].tag) { still = true; break; }
            if (!still) {
                html += "</" + open_stack[i].name + ">";
                open_stack.erase(open_stack.begin() + i);
            }
        }

        // Open newly active tags (skip heading/outline paragraph tags — handled at para level)
        for (auto& t : tags_here) {
            bool already = false;
            for (auto& o : open_stack)
                if (o.tag == t) { already = true; break; }
            if (already) continue;

            std::string tn = t->property_name().get_value();
            // Skip outline-level and sp- tags — paragraph-level, not inline
            if (tn.size() > 14 && tn.substr(0, 14) == "outline-level-") continue;
            if (tn.size() > 3  && tn.substr(0, 3)  == "sp-")           continue;

            std::string oh;
            if      (tn == "bold")            { oh = "<b>";  open_stack.push_back({"b",    t}); }
            else if (tn == "italic")          { oh = "<i>";  open_stack.push_back({"i",    t}); }
            else if (tn == "underline")       { oh = "<u>";  open_stack.push_back({"u",    t}); }
            else if (tn == "strikethrough")   { oh = "<s>";  open_stack.push_back({"s",    t}); }
            else if (tn == "justify_center")  { oh = "<span style=\"text-align:center\">";  open_stack.push_back({"span", t}); }
            else if (tn == "justify_right")   { oh = "<span style=\"text-align:right\">";   open_stack.push_back({"span", t}); }
            else if (tn == "justify_full")    { oh = "<span style=\"text-align:justify\">"; open_stack.push_back({"span", t}); }
            else if (tn.size() > 3 && tn.substr(0, 3) == "lh:") {
                oh = "<span style=\"line-height:" + tn.substr(3) + "\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 3 && tn.substr(0, 3) == "fg:") {
                oh = "<span style=\"color:" + tn.substr(3) + "\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 3 && tn.substr(0, 3) == "bg:") {
                oh = "<span style=\"background-color:" + tn.substr(3) + "\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() >= 5 && tn.substr(0, 5) == "font:") {
                std::string rest = tn.substr(5);
                auto colon = rest.rfind(':');
                std::string fam = (colon != std::string::npos) ? rest.substr(0, colon) : rest;
                std::string sz  = (colon != std::string::npos) ? rest.substr(colon + 1) : "";
                oh = "<span style=\"font-family:'" + fam + "';font-size:" + sz + "pt\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 3 && tn.substr(0, 3) == "li:") {
                oh = "<span style=\"margin-left:" + tn.substr(3) + "px\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 3 && tn.substr(0, 3) == "ri:") {
                oh = "<span style=\"margin-right:" + tn.substr(3) + "px\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 12 && tn.substr(0, 12) == "folio-style:") {
                std::string sname = tn.substr(12);
                oh = "<span data-folio-style=\"" + sname + "\">";
                open_stack.push_back({"span", t});
            } else if (tn.size() > 5 && tn.substr(0, 5) == "link:") {
                // Internal hyperlink: tag name = "link:<node_id>:<anchor_id>"
                std::string target = tn.substr(5); // "node_id:anchor_id"
                oh = "<a data-folio-link=\"" + target + "\">";
                open_stack.push_back({"a", t});
            }
            if (!oh.empty()) html += oh;
        }

        gunichar uc = cur.get_char();
        if (uc == '\n') {
            for (int i = (int)open_stack.size() - 1; i >= 0; --i)
                html += "</" + open_stack[i].name + ">";
            open_stack.clear();
            html += "</" + para_close(cur_para) + ">\n";
            // Determine the wrapper for the next paragraph
            auto next = cur; next.forward_char();
            cur_para = (next != end) ? para_tag(next) : "p";
            html += "<" + cur_para + ">";
        } else {
            char buf[7] = {};
            int len = g_unichar_to_utf8(uc, buf);
            html += html_escape(std::string(buf, len));
        }
        cur.forward_char();
    }
    for (int i = (int)open_stack.size() - 1; i >= 0; --i)
        html += "</" + open_stack[i].name + ">";
    html += "</" + para_close(cur_para) + ">";
    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// from_html — deserialize HTML string into buffer
// ─────────────────────────────────────────────────────────────────────────────

void EditorHtmlSerializer::from_html(const std::string& html) {
    m_buffer->set_text("");
    if (html.empty()) return;
    if (html.find('<') == std::string::npos) {
        m_buffer->set_text(html);
        return;
    }
    struct TagState { std::string tag, style; int offset = 0; };
    struct TagRange { std::string tag, style; int start = 0, end = 0; };
    std::vector<TagState> stack;
    std::vector<TagRange> ranges;
    std::string plain;
    plain.reserve(html.size());

    auto decode = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        size_t j = 0;
        while (j < s.size()) {
            if (s[j] == '&') {
                if      (s.substr(j, 5) == "&amp;")  { out += '&'; j += 5; }
                else if (s.substr(j, 4) == "&lt;")   { out += '<'; j += 4; }
                else if (s.substr(j, 4) == "&gt;")   { out += '>'; j += 4; }
                else if (s.substr(j, 6) == "&quot;") { out += '"'; j += 6; }
                else out += s[j++];
            } else {
                out += s[j++];
            }
        }
        return out;
    };

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') { plain += html[i++]; continue; }
        size_t et = html.find('>', i);
        if (et == std::string::npos) { plain += html[i++]; continue; }
        std::string tc = html.substr(i + 1, et - i - 1);
        i = et + 1;
        if (tc.empty()) continue;
        bool closing = (tc[0] == '/');
        if (closing) tc = tc.substr(1);
        std::string tname;
        size_t sp = tc.find_first_of(" \t");
        tname = (sp != std::string::npos) ? tc.substr(0, sp) : tc;
        for (auto& c : tname) c = std::tolower((unsigned char)c);

        if (!closing) {
            if (tname == "p" || tname == "br") {
                if (!plain.empty() && plain.back() != '\n') plain += '\n';
                // Check for data-sp screenplay element attribute
                size_t sp_pos = tc.find("data-sp=");
                if (sp_pos != std::string::npos) {
                    sp_pos += 8;
                    char q = tc[sp_pos++];
                    size_t ep = tc.find(q, sp_pos);
                    if (ep != std::string::npos) {
                        std::string el = tc.substr(sp_pos, ep - sp_pos);
                        stack.push_back({"sp-" + el, "", (int)plain.size()});
                    }
                }
                // Check for data-ol outline level attribute
                size_t ol_pos = tc.find("data-ol=");
                if (ol_pos != std::string::npos) {
                    ol_pos += 8;
                    char q = tc[ol_pos++];
                    size_t ep = tc.find(q, ol_pos);
                    if (ep != std::string::npos) {
                        std::string lv = tc.substr(ol_pos, ep - ol_pos);
                        stack.push_back({"outline-level-" + lv, "", (int)plain.size()});
                    }
                }
                // Check for data-anchor paragraph anchor attribute
                size_t an_pos = tc.find("data-anchor=");
                if (an_pos != std::string::npos) {
                    an_pos += 12;
                    char q = tc[an_pos++];
                    size_t ep = tc.find(q, an_pos);
                    if (ep != std::string::npos) {
                        std::string aid = tc.substr(an_pos, ep - an_pos);
                        stack.push_back({"anchor:" + aid, "", (int)plain.size()});
                    }
                }
            } else if (tname == "b" || tname == "i" || tname == "u" || tname == "s") {
                stack.push_back({tname, "", (int)plain.size()});
            } else if (tname == "a") {
                // Internal hyperlink
                size_t lp = tc.find("data-folio-link=");
                if (lp != std::string::npos) {
                    lp += 16;
                    char q = tc[lp++];
                    size_t ep = tc.find(q, lp);
                    if (ep != std::string::npos) {
                        std::string target = tc.substr(lp, ep - lp);
                        stack.push_back({"link:" + target, "", (int)plain.size()});
                    }
                }
            } else if (tname == "span") {
                std::string sv, dfs;
                size_t spos = tc.find("style=");
                if (spos != std::string::npos) {
                    spos += 6;
                    char q = tc[spos++];
                    size_t ep = tc.find(q, spos);
                    if (ep != std::string::npos) sv = tc.substr(spos, ep - spos);
                }
                size_t dpos = tc.find("data-folio-style=");
                if (dpos != std::string::npos) {
                    dpos += 17;
                    char q = tc[dpos++];
                    size_t ep = tc.find(q, dpos);
                    if (ep != std::string::npos) dfs = tc.substr(dpos, ep - dpos);
                }
                if (!dfs.empty()) sv = "folio-style:" + dfs;
                stack.push_back({"span", sv, (int)plain.size()});
            }
        } else {
            // Closing tag
            if (tname == "p") {
                // Close any open sp- or outline-level range for this paragraph
                for (int k = (int)stack.size() - 1; k >= 0; --k) {
                    auto& s = stack[k];
                    bool is_para_tag = (s.tag.size() > 3  && s.tag.substr(0, 3)  == "sp-") ||
                                       (s.tag.size() > 14 && s.tag.substr(0, 14) == "outline-level-") ||
                                       (s.tag.size() > 7  && s.tag.substr(0, 7)  == "anchor:");
                    if (is_para_tag) {
                        ranges.push_back({s.tag, s.style, s.offset, (int)plain.size()});
                        stack.erase(stack.begin() + k);
                        break;
                    }
                }
            } else {
                for (int k = (int)stack.size() - 1; k >= 0; --k) {
                    // "link:<node>:<anchor>" tags are opened as <a> and closed as </a>
                    bool match = (stack[k].tag == tname) ||
                                 (tname == "a" && stack[k].tag.size() > 5 &&
                                  stack[k].tag.substr(0, 5) == "link:");
                    if (match) {
                        ranges.push_back({stack[k].tag, stack[k].style,
                                          stack[k].offset, (int)plain.size()});
                        stack.erase(stack.begin() + k);
                        break;
                    }
                }
            }
        }
    }

    if (!plain.empty() && plain.back() == '\n') plain.pop_back();
    m_buffer->set_text(decode(plain));

    auto pv = [](const std::string& css, const std::string& prop) {
        size_t pos = css.find(prop);
        if (pos == std::string::npos) return std::string();
        pos += prop.size();
        while (pos < css.size() && (css[pos] == ' ' || css[pos] == '\'' || css[pos] == '"'))
            ++pos;
        std::string val;
        while (pos < css.size() && css[pos] != ';' && css[pos] != '\'' && css[pos] != '"')
            val += css[pos++];
        return val;
    };

    for (auto& r : ranges) {
        if (r.start >= r.end) continue;
        auto s = m_buffer->get_iter_at_offset(r.start);
        auto e = m_buffer->get_iter_at_offset(r.end);

        if      (r.tag == "b") m_buffer->apply_tag(m_tags.bold,          s, e);
        else if (r.tag == "i") m_buffer->apply_tag(m_tags.italic,        s, e);
        else if (r.tag == "u") m_buffer->apply_tag(m_tags.underline,     s, e);
        else if (r.tag == "s") m_buffer->apply_tag(m_tags.strikethrough, s, e);
        else if (r.tag.size() > 3 && r.tag.substr(0, 3) == "sp-") {
            auto st = m_buffer->get_tag_table()->lookup(r.tag);
            LOG_DEBUG("from_html: sp tag '{}' lookup={}", r.tag, st ? "found" : "NOT FOUND");
            if (st) m_buffer->apply_tag(st, s, e);
        }
        else if (r.tag.size() > 14 && r.tag.substr(0, 14) == "outline-level-") {
            int lv = std::stoi(r.tag.substr(14)) - 1; // 0-based index
            if (lv >= 0 && lv < (int)m_tags.ol.size() && m_tags.ol[lv])
                m_buffer->apply_tag(m_tags.ol[lv], s, e);
        }
        else if (r.tag.size() > 7 && r.tag.substr(0, 7) == "anchor:") {
            // Paragraph anchor — create or reuse a tag named "anchor:<id>"
            std::string tname = r.tag; // already "anchor:<id>"
            auto tt = m_buffer->get_tag_table()->lookup(tname);
            if (!tt) tt = m_buffer->create_tag(tname);
            m_buffer->apply_tag(tt, s, e);
        }
        else if (r.tag.size() > 5 && r.tag.substr(0, 5) == "link:") {
            // Internal hyperlink — create or reuse tag named "link:<node>:<anchor>"
            std::string tname = r.tag;
            auto tt = m_buffer->get_tag_table()->lookup(tname);
            if (!tt) {
                tt = m_buffer->create_tag(tname);
                // Visual style applied by Editor after load via apply_link_tag_style()
            }
            m_buffer->apply_tag(tt, s, e);
            LOG_DEBUG("from_html: link tag '{}' applied", tname);
        }
        else if (r.tag == "span") {
            // Named folio style
            if (r.style.size() > 12 && r.style.substr(0, 12) == "folio-style:") {
                std::string sname = r.style.substr(12);
                std::string stn   = "folio-style:" + sname;
                auto st = m_buffer->get_tag_table()->lookup(stn);
                if (!st) st = m_buffer->create_tag(stn);
                m_buffer->apply_tag(st, s, e);
                continue;
            }

            std::string ta = pv(r.style, "text-align:");
            if      (ta == "center")  { m_buffer->apply_tag(m_tags.justify_center, s, e); continue; }
            else if (ta == "right")   { m_buffer->apply_tag(m_tags.justify_right,  s, e); continue; }
            else if (ta == "justify") { m_buffer->apply_tag(m_tags.justify_full,   s, e); continue; }

            std::string lh = pv(r.style, "line-height:");
            if (!lh.empty()) {
                std::string tn = "lh:" + lh;
                auto tt = m_buffer->get_tag_table()->lookup(tn);
                if (!tt) {
                    tt = m_buffer->create_tag(tn);
                    try { tt->property_line_height() = (float)std::stod(lh); } catch (...) {}
                }
                m_buffer->apply_tag(tt, s, e);
                continue;
            }

            std::string fg = pv(r.style, "color:");
            if (!fg.empty()) {
                std::string tn = "fg:" + fg;
                auto tt = m_buffer->get_tag_table()->lookup(tn);
                if (!tt) {
                    tt = m_buffer->create_tag(tn);
                    float rv = 0, gv = 0, bv = 0;
                    std::sscanf(fg.c_str(), "%f:%f:%f", &rv, &gv, &bv);
                    Gdk::RGBA rgba; rgba.set_rgba(rv, gv, bv, 1.0f);
                    tt->property_foreground_rgba() = rgba;
                }
                m_buffer->apply_tag(tt, s, e);
                continue;
            }

            std::string bg = pv(r.style, "background-color:");
            if (!bg.empty()) {
                std::string tn = "bg:" + bg;
                auto tt = m_buffer->get_tag_table()->lookup(tn);
                if (!tt) {
                    tt = m_buffer->create_tag(tn);
                    float rv = 0, gv = 0, bv = 0;
                    std::sscanf(bg.c_str(), "%f:%f:%f", &rv, &gv, &bv);
                    Gdk::RGBA rgba; rgba.set_rgba(rv, gv, bv, 1.0f);
                    tt->property_background_rgba() = rgba;
                }
                m_buffer->apply_tag(tt, s, e);
                continue;
            }

            std::string ml = pv(r.style, "margin-left:");
            if (!ml.empty()) {
                while (!ml.empty() && (ml.back() == 'x' || ml.back() == 'p')) ml.pop_back();
                try {
                    int px = std::stoi(ml);
                    std::string tn = "li:" + std::to_string(px);
                    auto tt = m_buffer->get_tag_table()->lookup(tn);
                    if (!tt) {
                        tt = m_buffer->create_tag(tn);
                        tt->property_left_margin() = px;
                    }
                    m_buffer->apply_tag(tt, s, e);
                } catch (...) {}
                continue;
            }

            std::string mr = pv(r.style, "margin-right:");
            if (!mr.empty()) {
                while (!mr.empty() && (mr.back() == 'x' || mr.back() == 'p')) mr.pop_back();
                try {
                    int px = std::stoi(mr);
                    std::string tn = "ri:" + std::to_string(px);
                    auto tt = m_buffer->get_tag_table()->lookup(tn);
                    if (!tt) {
                        tt = m_buffer->create_tag(tn);
                        tt->property_right_margin() = px;
                    }
                    m_buffer->apply_tag(tt, s, e);
                } catch (...) {}
                continue;
            }

            std::string fam = pv(r.style, "font-family:");
            std::string sz  = pv(r.style, "font-size:");
            while (!sz.empty() && (sz.back() == 't' || sz.back() == 'p')) sz.pop_back();
            if (!fam.empty() && !sz.empty()) {
                try {
                    int pt = std::stoi(sz);
                    std::string tn = "font:" + fam + ":" + std::to_string(pt);
                    auto tt = m_buffer->get_tag_table()->lookup(tn);
                    if (!tt) {
                        tt = m_buffer->create_tag(tn);
                        tt->property_family()      = fam;
                        tt->property_size_points() = (double)pt;
                    }
                    m_buffer->apply_tag(tt, s, e);
                } catch (...) {}
            }
        }
    }

    // ── Paragraph-tag repair pass ─────────────────────────────────────────────
    // li: and ri: tags are paragraph-level — they must cover the entire
    // paragraph.  Text substitution (smart quotes, em-dash) can leave the
    // first character of a paragraph uncovered if the substitution happened
    // at paragraph start.  Walk each paragraph: if any character in it has a
    // li:/ri: tag, extend that tag to cover the whole paragraph.
    {
        auto line_start = m_buffer->begin();
        while (!line_start.is_end()) {
            auto line_end = line_start;
            // Walk to end of this line (stop before the \n)
            if (!line_end.forward_to_line_end())
                line_end = m_buffer->end();

            // Collect li:/ri: tags active anywhere in this line
            std::vector<Glib::RefPtr<Gtk::TextTag>> para_tags;
            auto it = line_start;
            while (it != line_end && !it.is_end()) {
                for (auto& tag : it.get_tags()) {
                    const std::string& n = tag->property_name().get_value();
                    if (n.size() > 3 && (n.substr(0,3) == "li:" || n.substr(0,3) == "ri:")) {
                        bool found = false;
                        for (auto& t : para_tags) if (t == tag) { found = true; break; }
                        if (!found) para_tags.push_back(tag);
                    }
                }
                if (!it.forward_to_tag_toggle(Glib::RefPtr<Gtk::TextTag>{}))
                    break;
                if (it > line_end) break;
            }

            // Apply each found tag across the entire line
            for (auto& tag : para_tags)
                m_buffer->apply_tag(tag, line_start, line_end);

            if (!line_start.forward_line()) break;
        }
    }
}

} // namespace Folio
