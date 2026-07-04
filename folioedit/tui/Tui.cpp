// ─────────────────────────────────────────────────────────────────────────────
// folioedit — Tui.cpp   (slice 1.6: focus border + a real text cursor)
//
// Adds to slice 1.5: the focused pane borders in the accent so you can see where
// you are, and the viewer carries a visible block caret you drive char-to-char
// (← →), line-to-line (↑ ↓), Home/End, PgUp/PgDn, and paragraph-to-paragraph
// (Ctrl+↑ / Ctrl+↓). The view follows the caret, which also fixes "won't scroll
// to the bottom" (the clamp is now keep-the-caret-visible, not a fixed count).
// The caret is UTF-8 aware and the viewer does its own word wrap so the cursor
// tracks soft-wrap boundaries. FTXUI over the existing engine; no new crypto.
// ─────────────────────────────────────────────────────────────────────────────
#include "Tui.hpp"

#include "folioedit/Archive.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Identity.hpp"    // s107 — KeyPair / sign_event / fingerprint (Seal)
#include "folioedit/Custody.hpp"     // s107 — CustodyEvent / append_event (Seal)

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fe = folioedit;
namespace fs = std::filesystem;
using namespace ftxui;

namespace {

// A minimal scroll container for read-only content taller than its box: PgUp/PgDn,
// arrows, Home/End, and the mouse wheel move a viewport over the child, with a
// scrollbar. (The canonical FTXUI Scroller idiom — the library ships it only as an
// example, so we carry a small copy.) Used by the help overlay so a long help body
// scrolls inside a bounded window and the Close button always has room to draw.
class ScrollerBase : public ComponentBase {
public:
    explicit ScrollerBase(Component child) { Add(std::move(child)); }

    Element Render() final {
        Element background = ComponentBase::Render();
        background->ComputeRequirement();
        size_ = background->requirement().min_y;
        if (size_ == 0) size_ = 1;
        return dbox({
                   std::move(background),
                   vbox({text("") | size(HEIGHT, EQUAL, selected_), text("") | focus}),
               })
               | vscroll_indicator | yframe | reflect(box_);
    }

    bool OnEvent(Event event) final {
        if (event.is_mouse() && box_.Contain(event.mouse().x, event.mouse().y))
            TakeFocus();
        const int prev = selected_;
        const int page = std::max(1, box_.y_max - box_.y_min);
        if (event == Event::ArrowUp ||
            (event.is_mouse() && event.mouse().button == Mouse::WheelUp))          selected_--;
        else if (event == Event::ArrowDown ||
                 (event.is_mouse() && event.mouse().button == Mouse::WheelDown))   selected_++;
        else if (event == Event::PageUp)                                          selected_ -= page;
        else if (event == Event::PageDown)                                        selected_ += page;
        else if (event == Event::Home)                                            selected_ = 0;
        else if (event == Event::End)                                             selected_ = size_;
        else return ComponentBase::OnEvent(event);
        selected_ = std::max(0, std::min(size_ - 1, selected_));
        return selected_ != prev;
    }

    bool Focusable() const final { return true; }

private:
    int selected_ = 0;
    int size_     = 0;
    Box box_;
};
Component Scroller(Component child) { return Make<ScrollerBase>(std::move(child)); }

// ── UTF-8 stepping (caret columns are byte offsets on char boundaries) ───────
int utf8_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}
int utf8_next(const std::string& s, int i) {
    if (i >= (int)s.size()) return (int)s.size();
    return std::min((int)s.size(), i + utf8_len((unsigned char)s[(std::size_t)i]));
}
int utf8_prev(const std::string& s, int i) {
    if (i <= 0) return 0;
    int j = i - 1;
    while (j > 0 && ((unsigned char)s[(std::size_t)j] & 0xC0) == 0x80) j--;
    return j;
}
int utf8_snap(const std::string& s, int i) {   // pull to the nearest boundary ≤ i
    i = std::max(0, std::min(i, (int)s.size()));
    while (i > 0 && i < (int)s.size() && ((unsigned char)s[(std::size_t)i] & 0xC0) == 0x80) i--;
    return i;
}
int utf8_glyphs(const std::string& s) {
    int n = 0;
    for (int i = 0; i < (int)s.size(); i = utf8_next(s, i)) ++n;
    return n;
}

// A UTF-8-safe leading slice of at most `maxg` glyphs, with an ellipsis if cut.
std::string utf8_prefix(const std::string& s, int maxg) {
    int i = 0, g = 0;
    while (i < (int)s.size() && g < maxg) { i = utf8_next(s, i); ++g; }
    if (i >= (int)s.size()) return s;
    return s.substr(0, (std::size_t)i) + "\u2026";
}

// Greedy word-wrap `text` to `width` display columns (approx: 1 col/glyph).
std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> out;
    if (width < 1) width = 1;
    std::string line;
    int line_w = 0;
    std::string word;
    int word_w = 0;
    auto flush_word = [&] {
        if (word.empty()) return;
        if (line_w > 0 && line_w + 1 + word_w > width) { out.push_back(line); line.clear(); line_w = 0; }
        if (line_w > 0) { line += ' '; line_w += 1; }
        // a word longer than the whole width: hard-break it
        while (word_w > width) {
            int taken = 0, i = 0;
            while (i < (int)word.size() && taken < width) { i = utf8_next(word, i); taken++; }
            out.push_back(word.substr(0, (std::size_t)i));
            word = word.substr((std::size_t)i);
            word_w = utf8_glyphs(word);
        }
        line += word; line_w += word_w; word.clear(); word_w = 0;
    };
    for (int i = 0; i < (int)text.size(); ) {
        int j = utf8_next(text, i);
        if (text[(std::size_t)i] == ' ') { flush_word(); }
        else { word += text.substr((std::size_t)i, (std::size_t)(j - i)); word_w++; }
        i = j;
    }
    flush_word();
    if (line_w > 0 || out.empty()) out.push_back(line);
    return out;
}

struct SourcePara { std::string text; Color color = Color::Default; bool para_start = false; bool prose = false; };
struct VisLine    { std::string text; Color color = Color::Default; bool para_start = false; bool prose = false; };

// A marked range resolved to the scene's visible-text space: char offsets +
// the exact spanned text (the quote is the robust anchor — absorb re-anchors by
// it when offsets drift; §16.6).
struct Selection { int start = 0; int end = 0; std::string quote; bool ok = false; };

// ── a focusable viewer with a driven text caret ──────────────────────────────
class ViewerBase : public ComponentBase {
public:
    explicit ViewerBase(std::function<std::vector<SourcePara>()> provider)
        : provider_(std::move(provider)) {}

    Element Render() final {
        int width  = (box_.x_max >= box_.x_min && box_.x_max > 0) ? (box_.x_max - box_.x_min + 1) : 78;
        int height = (box_.y_max >= box_.y_min && box_.y_max > 0) ? (box_.y_max - box_.y_min + 1) : 20;
        if (width  < 4) width  = 78;
        if (height < 1) height = 20;
        page_ = height;

        // flatten current content into visual (wrapped) lines
        flat_.clear();
        for (const SourcePara& p : provider_()) {
            std::vector<std::string> wl = wrap(p.text, width);
            if (wl.empty()) wl.push_back("");
            for (std::size_t k = 0; k < wl.size(); ++k)
                flat_.push_back({wl[k], p.color, (k == 0 && p.para_start), p.prose});
        }
        if (flat_.empty()) flat_.push_back({"", Color::Default, false, false});
        const int n = (int)flat_.size();

        caret_line_ = std::max(0, std::min(caret_line_, n - 1));
        caret_col_  = utf8_snap(flat_[(std::size_t)caret_line_].text, caret_col_);

        if (caret_line_ < scroll_top_)                 scroll_top_ = caret_line_;
        if (caret_line_ >= scroll_top_ + height)       scroll_top_ = caret_line_ - height + 1;
        scroll_top_ = std::max(0, std::min(scroll_top_, std::max(0, n - height)));

        const bool foc = Focused();
        Elements rows;
        for (int i = scroll_top_; i < n && i < scroll_top_ + height; ++i)
            rows.push_back(render_line(flat_[(std::size_t)i], i, foc));
        return vbox(std::move(rows)) | reflect(box_);
    }

    bool OnEvent(Event event) final {
        const int n = (int)flat_.size();
        auto len = [&](int i) { return (i >= 0 && i < n) ? (int)flat_[(std::size_t)i].text.size() : 0; };

        if (event.is_mouse()) {
            if (!box_.Contain(event.mouse().x, event.mouse().y)) return false;
            TakeFocus();
            if (event.mouse().button == Mouse::WheelUp   && caret_line_ > 0)     caret_line_--;
            if (event.mouse().button == Mouse::WheelDown && caret_line_ < n - 1) caret_line_++;
            return true;
        }
        if (!Focused()) return false;

        bool handled = true;
        if (event == Event::ArrowLeft) {
            if (caret_col_ > 0) caret_col_ = utf8_prev(flat_[(std::size_t)caret_line_].text, caret_col_);
            else if (caret_line_ > 0) { caret_line_--; caret_col_ = len(caret_line_); }
        } else if (event == Event::ArrowRight) {
            if (caret_col_ < len(caret_line_)) caret_col_ = utf8_next(flat_[(std::size_t)caret_line_].text, caret_col_);
            else if (caret_line_ < n - 1) { caret_line_++; caret_col_ = 0; }
        } else if (event == Event::ArrowUp) {
            if (caret_line_ > 0) caret_line_--;
        } else if (event == Event::ArrowDown) {
            if (caret_line_ < n - 1) caret_line_++;
        } else if (event == Event::Home) {
            caret_col_ = 0;
        } else if (event == Event::End) {
            caret_col_ = len(caret_line_);
        } else if (event == Event::PageUp) {
            caret_line_ = std::max(0, caret_line_ - page_);
        } else if (event == Event::PageDown) {
            caret_line_ = std::min(n - 1, caret_line_ + page_);
        } else if (event == Event::ArrowUpCtrl) {
            int i = caret_line_ - 1;
            while (i > 0 && !flat_[(std::size_t)i].para_start) i--;
            caret_line_ = std::max(0, i); caret_col_ = 0;
        } else if (event == Event::ArrowDownCtrl) {
            int i = caret_line_ + 1;
            while (i < n && !flat_[(std::size_t)i].para_start) i++;
            caret_line_ = std::min(n - 1, i); caret_col_ = 0;
        } else {
            handled = false;
        }
        caret_col_ = utf8_snap(flat_[(std::size_t)caret_line_].text, caret_col_);
        return handled;
    }

    bool Focusable() const final { return true; }

    // ── s107 marking (Mark → Set) ────────────────────────────────────────────
    // Mark drops the start of the range at the caret; the caret then extends it.
    void begin_mark() { marking_ = true; anchor_line_ = caret_line_; anchor_col_ = caret_col_; }
    void clear_mark() { marking_ = false; }
    bool marking() const { return marking_; }

    // Resolve the current mark to the scene's visible-text char range + quote.
    // Walks the flattened PROSE lines (skipping title/notes chrome), rejoining
    // wrapped pieces of a paragraph with a space and paragraph breaks with "\n\n",
    // so offsets/quote live in the same visible space Folio's absorb re-anchors in.
    Selection selection() const {
        Selection s;
        if (!marking_) return s;
        int al = anchor_line_, ac = anchor_col_, cl = caret_line_, cc = caret_col_;
        if (cl < al || (cl == al && cc < ac)) { std::swap(al, cl); std::swap(ac, cc); }

        std::string V;
        std::vector<int> line_off(flat_.size(), -1);   // -1 = non-prose line
        bool first = true;
        for (std::size_t i = 0; i < flat_.size(); ++i) {
            if (!flat_[i].prose) continue;
            if (!first) V += (flat_[i].para_start ? "\n\n" : " ");
            line_off[i] = static_cast<int>(V.size());
            V += flat_[i].text;
            first = false;
        }
        if (al < 0 || cl < 0 || static_cast<std::size_t>(cl) >= flat_.size()) return s;
        if (line_off[static_cast<std::size_t>(al)] < 0 ||
            line_off[static_cast<std::size_t>(cl)] < 0)
            return s;   // an endpoint isn't in the prose — not a valid text range

        const int bstart = line_off[static_cast<std::size_t>(al)] +
                           std::min(ac, static_cast<int>(flat_[static_cast<std::size_t>(al)].text.size()));
        const int bend   = line_off[static_cast<std::size_t>(cl)] +
                           std::min(cc, static_cast<int>(flat_[static_cast<std::size_t>(cl)].text.size()));
        if (bend <= bstart) return s;

        s.quote = V.substr(static_cast<std::size_t>(bstart), static_cast<std::size_t>(bend - bstart));
        s.start = utf8_glyphs(V.substr(0, static_cast<std::size_t>(bstart)));
        s.end   = s.start + utf8_glyphs(s.quote);
        s.ok    = true;
        return s;
    }

private:
    Element render_line(const VisLine& vl, int idx, bool foc) {
        const std::string& s = vl.text;
        const int sz = static_cast<int>(s.size());

        // selection span [slo,shi) on this line (bytes), if a mark is active
        int slo = -1, shi = -1;
        if (marking_) {
            int al = anchor_line_, ac = anchor_col_, cl = caret_line_, cc = caret_col_;
            if (cl < al || (cl == al && cc < ac)) { std::swap(al, cl); std::swap(ac, cc); }
            if (idx >= al && idx <= cl) {
                slo = (idx == al) ? std::min(ac, sz) : 0;
                shi = (idx == cl) ? std::min(cc, sz) : sz;
            }
        }
        const bool caret_here = foc && idx == caret_line_;
        const int  car = caret_here ? std::min(caret_col_, sz) : -1;

        // Cut the line at selection + caret boundaries, style each run.
        std::vector<int> cuts = {0, sz};
        if (slo >= 0) { cuts.push_back(slo); cuts.push_back(shi); }
        if (car >= 0) { cuts.push_back(car); cuts.push_back(std::min(utf8_next(s, car), sz)); }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

        Elements runs;
        for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
            const int a = cuts[i], b = cuts[i + 1];
            if (b <= a) continue;
            const bool is_caret = (car >= 0 && a == car);
            const bool is_sel   = (slo >= 0 && a >= slo && b <= shi);
            Element seg = text(s.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)));
            if (is_caret)     seg = seg | inverted;                       // the moving end
            else if (is_sel)  seg = seg | bgcolor(Color::GrayDark);       // the marked range
            runs.push_back(seg);
        }
        if (caret_here && car >= sz)   // caret past end-of-line: a lit cell
            runs.push_back(text(" ") | inverted);
        Element e = runs.empty() ? text("") : hbox(std::move(runs));
        if (vl.color != Color::Default) e = e | color(vl.color);
        return e;
    }

    std::function<std::vector<SourcePara>()> provider_;
    std::vector<VisLine> flat_;
    int caret_line_ = 0;
    int caret_col_  = 0;   // byte offset on a UTF-8 boundary
    int scroll_top_ = 0;
    int page_ = 20;
    bool marking_ = false; // s107 — a range is being marked (Mark → Set)
    int anchor_line_ = 0;  // where Mark dropped the start of the range
    int anchor_col_  = 0;
    Box box_;
};

bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::vector<std::string> html_paragraphs(const std::string& html) {
    std::vector<std::string> out;
    std::string cur;
    bool in_tag = false;
    auto flush = [&] {
        auto a = cur.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { cur.clear(); return; }
        auto b = cur.find_last_not_of(" \t\r\n");
        out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (std::size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '<') {
            if (html.compare(i, 3, "<p>")  == 0 || html.compare(i, 4, "</p>") == 0 ||
                html.compare(i, 4, "<br>") == 0 || html.compare(i, 5, "<br/>") == 0)
                flush();
            in_tag = true; continue;
        }
        if (c == '>') { in_tag = false; continue; }
        if (in_tag) continue;
        if (c == '&') {
            if      (html.compare(i, 5, "&amp;")  == 0) { cur += '&';  i += 4; }
            else if (html.compare(i, 4, "&lt;")   == 0) { cur += '<';  i += 3; }
            else if (html.compare(i, 4, "&gt;")   == 0) { cur += '>';  i += 3; }
            else if (html.compare(i, 6, "&quot;") == 0) { cur += '"';  i += 5; }
            else if (html.compare(i, 6, "&#39;")  == 0) { cur += '\''; i += 5; }
            else cur += c;
            continue;
        }
        cur += c;
    }
    flush();
    return out;
}

// ISO-8601 UTC (matches the CLI / Folio so timestamps read consistently).
std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

// The editor's TOFU identity keyfile:  <data>/folioedit/identity.key
//   <data> = $XDG_DATA_HOME, else $HOME/.local/share  (mirrors Folio's Interchange).
// Load it, or mint + persist a fresh Ed25519 keypair on first Seal. Throws on
// I/O / crypto failure (the caller reports it in the status line).
fe::KeyPair load_or_create_identity() {
    fs::path base;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) base = fs::path(x);
    else if (const char* h = std::getenv("HOME");     h && *h) base = fs::path(h) / ".local" / "share";
    const fs::path path = base.empty() ? fs::path("folioedit-identity.key")
                                       : base / "folioedit" / "identity.key";
    std::error_code ec;
    if (fs::exists(path, ec) && !ec) return fe::load_keypair(path.string());
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fe::KeyPair kp = fe::generate_keypair();
    fe::save_keypair(kp, path.string());
    return kp;
}

}  // namespace

namespace folioedit {

int run_tui(const std::string& initial_file) {
    auto screen = ScreenInteractive::Fullscreen();

    fe::Document doc;
    bool        have_doc = false;
    std::string editor_name;
    std::string status = "No pass open.";

    std::vector<std::string> scene_titles;
    int scene_selected = 0;

    bool open_shown = !initial_file.empty();
    bool help_shown = false;
    std::string in_file = initial_file, in_pass, in_name, modal_error;

    // ── file browser ─────────────────────────────────────────────────────────
    fs::path browse_dir = fs::current_path();
    std::vector<std::string> browse_labels, browse_paths;
    std::vector<char> browse_isdir;
    int browse_selected = 0;
    auto relist = [&] {
        browse_labels.clear(); browse_paths.clear(); browse_isdir.clear(); browse_selected = 0;
        if (browse_dir.has_parent_path() && browse_dir.parent_path() != browse_dir) {
            browse_labels.push_back("../");
            browse_paths.push_back(browse_dir.parent_path().string());
            browse_isdir.push_back(1);
        }
        std::vector<std::pair<std::string,std::string>> dirs, files;
        try {
            for (const auto& e : fs::directory_iterator(browse_dir)) {
                const std::string name = e.path().filename().string();
                if (!name.empty() && name[0] == '.') continue;
                std::error_code ec;
                if (e.is_directory(ec)) dirs.push_back({name + "/", e.path().string()});
                else if (ends_with(name, ".folioedit")) files.push_back({name, e.path().string()});
            }
        } catch (...) {}
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        for (auto& d : dirs)  { browse_labels.push_back(d.first); browse_paths.push_back(d.second); browse_isdir.push_back(1); }
        for (auto& f : files) { browse_labels.push_back(f.first); browse_paths.push_back(f.second); browse_isdir.push_back(0); }
    };
    relist();

    auto fill_from_doc = [&] {
        scene_titles.clear();
        for (std::size_t i = 0; i < doc.scenes.size(); ++i)
            scene_titles.push_back(std::to_string(i + 1) + "  " + doc.scenes[i].title);
        scene_selected = 0;
    };
    auto do_load = [&] {
        modal_error.clear();
        try {
            const fe::FileFace face = fe::peek_file_face(in_file);
            if (face == fe::FileFace::Plain)       doc = fe::open_document_plain(in_file);
            else if (face == fe::FileFace::Sealed) doc = fe::open_document_pw(in_file, in_pass);
            else { modal_error = "That isn't a .folioedit file."; return; }
            editor_name = in_name; have_doc = true; fill_from_doc(); open_shown = false;
            status = "Opened " + doc.project_title + " — " +
                     std::to_string(doc.scenes.size()) + " scenes, " +
                     std::to_string(doc.annotations.size()) + " annotations." +
                     (editor_name.empty() ? "" : ("  Editor: " + editor_name));
        } catch (const std::exception& e) {
            modal_error = std::string("Couldn't open: ") + e.what() +
                          "  (check the passphrase — spacing and case don't matter).";
        }
    };

    MenuOption browse_opt;
    browse_opt.on_enter = [&] {
        if (browse_selected < 0 || browse_selected >= (int)browse_labels.size()) return;
        if (browse_isdir[(std::size_t)browse_selected]) { browse_dir = browse_paths[(std::size_t)browse_selected]; relist(); }
        else in_file = browse_paths[(std::size_t)browse_selected];
    };
    auto browse_menu = Menu(&browse_labels, &browse_selected, browse_opt);
    auto file_in = Input(&in_file, "…or type a path to the .folioedit file");
    InputOption pw_opt; pw_opt.password = true;
    auto pass_in = Input(&in_pass, "the four-word passphrase", pw_opt);
    auto name_in = Input(&in_name, "your name (signs your notes)");
    auto load_btn   = Button("Load",   do_load);
    auto cancel_btn = Button("Cancel", [&] { open_shown = false; });
    auto open_container = Container::Vertical({
        browse_menu, file_in, pass_in, name_in, Container::Horizontal({load_btn, cancel_btn}),
    });
    auto open_modal = Renderer(open_container, [&] {
        return vbox({
                   text("Open a pass") | bold, separator(),
                   text("Folder: " + browse_dir.string()) | dim,
                   browse_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 10) | border,
                   hbox(text("File:       "), file_in->Render() | flex),
                   hbox(text("Passphrase: "), pass_in->Render() | flex),
                   hbox(text("Your name:  "), name_in->Render() | flex),
                   (modal_error.empty() ? text("") : text(modal_error) | color(Color::Red)),
                   separator(),
                   hbox(load_btn->Render(), text("  "), cancel_btn->Render()),
                   text("Enter a folder to open it, or pick a .folioedit file.") | dim,
               }) | border | size(WIDTH, GREATER_THAN, 60) | bgcolor(Color::Black);
    });

    auto help_close = Button("Close", [&] { help_shown = false; });
    // The help BODY as its own renderer, wrapped in a Scroller so a long body
    // scrolls inside a bounded window instead of shoving the Close button off the
    // bottom of the screen (the s106-TUI bug: on a normal-height terminal the
    // Close row drew below the fold). Title + Close stay pinned outside the scroll.
    auto help_body = Renderer([&] {
        auto row = [](std::string k, std::string v) {
            return hbox({text(k) | color(Color::Plum1) | size(WIDTH, EQUAL, 22), text(v)});
        };
        return vbox({
                   text("Workflow:  Open a pass → read the scenes → mark a range, "
                        "write a note, Set → Seal → Save.") | dim,
                   text(""),
                   row("Tab / Shift+Tab", "move between the list, the text, and the buttons"),
                   row("\u2190 \u2192", "move the cursor by character"),
                   row("\u2191 \u2193", "move the cursor by line"),
                   row("Ctrl+\u2191 / Ctrl+\u2193", "jump paragraph to paragraph"),
                   row("Home / End", "start / end of the line"),
                   row("PgUp / PgDn", "scroll a screenful"),
                   row("Enter", "activate the focused button / list row"),
                   row("Open   O", "choose a pass to load"),
                   row("Prev / Next", "previous / next scene"),
                   row("Mark   m", "drop the start of a range at the cursor"),
                   row("Set    s", "write a note for the marked range"),
                   row("Tab", "move focus: scenes \u2192 text \u2192 Notes \u2192 buttons"),
                   row("Help   ?  /  F1", "this screen"),
                   row("Quit   Q", "leave folioedit"),
                   row("Esc", "close a dialog · cancel a mark in progress"),
                   text(""),
                   text("Reviewing notes:") | color(Color::Plum1),
                   row("  Notes panel", "the right panel lists THIS scene's notes \u2014 verdict"),
                   row("", "glyph, range, the quoted span, your comment. Tab to it"),
                   row("", "and \u2191\u2193 to select; the full note opens below the text."),
                   row("  \u25B8 / \u2713 / \u2717", "proposed / accepted / declined (the author's verdict)."),
                   text(""),
                   text("The editing loop:") | color(Color::Plum1),
                   row("  Mark", "put the cursor at the start of a passage and press Mark"),
                   row("", "(m). Move the cursor to the end — the highlight is the"),
                   row("", "range your note will attach to. Esc cancels it."),
                   row("  Set", "press Set (s), type your comment, File it. The note is"),
                   row("", "filed as a PROPOSAL (\u25B8) — it never rewrites the prose;"),
                   row("", "the author accepts (\u2713) or declines (\u2717) it back in Folio."),
                   row("  Seal", "sign the file with your name so the author can prove it"),
                   row("", "came from you, untampered, with a chain of custody. Your"),
                   row("", "identity key is made on first Seal and reused after."),
                   row("  Save", "write everything — your notes and the seal — back into"),
                   row("", "the .folioedit file (in place) to send home."),
                   text(""),
                   text("Verdict glyphs (on notes in a returned pass):") | color(Color::Plum1),
                   row("  \u25B8 proposed", "awaiting the author's decision"),
                   row("  \u2713 accepted", "the author will address it"),
                   row("  \u2717 declined", "the author kept the prose as written"),
               });
    });
    auto help_scroller = Scroller(help_body);
    auto help_box = Container::Vertical({help_scroller, help_close});
    auto help_modal = Renderer(help_box, [&] {
        return vbox({
                   text("folioedit — help") | bold,
                   separator(),
                   help_scroller->Render() | size(HEIGHT, LESS_THAN, 16),
                   separator(),
                   hbox({text("\u2191\u2193 / PgUp\u00b7PgDn scroll") | dim, filler(),
                         help_close->Render()}),
               }) | border | size(WIDTH, GREATER_THAN, 64) | bgcolor(Color::Black);
    });

    // ── viewer content provider ──────────────────────────────────────────────
    auto viewer_impl = std::make_shared<ViewerBase>([&]() -> std::vector<SourcePara> {
        std::vector<SourcePara> v;
        if (!have_doc) {
            v.push_back({"Welcome", Color::Plum1, true});
            v.push_back({"You've been sent a sealed pass to mark up. Nothing's loaded yet.", Color::Default, true});
            v.push_back({"To begin:", Color::GrayLight, true});
            v.push_back({"1  Click Open and choose the .folioedit file you were sent.", Color::Default, true});
            v.push_back({"2  Enter the passphrase the author gave you (the four words).", Color::Default, true});
            v.push_back({"3  Enter your name — it signs the notes you send back.", Color::Default, true});
            v.push_back({"The scenes and your annotation area open once the pass loads. Press ? for help.", Color::GrayDark, true});
            return v;
        }
        if (scene_selected < 0 || scene_selected >= (int)doc.scenes.size()) return v;
        const fe::Scene& sc = doc.scenes[(std::size_t)scene_selected];
        v.push_back({sc.title, Color::Plum1, true});
        v.push_back({"", Color::Default, false});                 // gap under the title
        const std::vector<std::string> paras = html_paragraphs(sc.text);
        for (std::size_t i = 0; i < paras.size(); ++i) {
            v.push_back({paras[i], Color::Default, true, true});   // prose (markable)
            if (i + 1 < paras.size()) v.push_back({"", Color::Default, false, false});   // between paras
        }
        return v;
    });
    Component viewer = viewer_impl;   // same object, as a Component for the layout

    // ── Set (write a note) modal state ────────────────────────────────────────
    bool        set_shown = false;
    bool        dirty     = false;   // unsaved notes / seal
    std::string set_note, set_kind, set_quote, set_error;
    Selection   set_sel;
    auto note_input = Input(&set_note, "Type your note for the marked range…");

    // ── annotations panel (right side, like the scene list on the left) ───────
    // Rebuilt each frame from the CURRENT scene's notes: one selectable row per
    // note — verdict glyph · range · quoted span · abbreviated note. Selecting a
    // row shows it in full in the Annotation panel under the text.
    std::vector<std::string> ann_titles;
    std::vector<int>         ann_index;      // row -> index into doc.annotations
    int                      ann_selected = 0;
    auto rebuild_ann_list = [&] {
        ann_titles.clear(); ann_index.clear();
        if (!have_doc || scene_selected < 0 || scene_selected >= (int)doc.scenes.size()) return;
        const std::string iid = doc.scenes[(std::size_t)scene_selected].iid;
        for (std::size_t i = 0; i < doc.annotations.size(); ++i) {
            const fe::Annotation& a = doc.annotations[i];
            if (a.scene_iid != iid || a.withdrawn) continue;
            const char* g = (a.verdict == fe::Verdict::Accepted) ? "\u2713"
                          : (a.verdict == fe::Verdict::Declined) ? "\u2717" : "\u25B8";
            std::string row = std::string(g) + " " +
                              std::to_string(a.range_start) + "\u2013" + std::to_string(a.range_end) +
                              " \u201c" + utf8_prefix(a.quote, 14) + "\u201d " +
                              utf8_prefix(a.text, 18);
            ann_index.push_back(static_cast<int>(i));
            ann_titles.push_back(row);
        }
        if (ann_selected >= (int)ann_titles.size())
            ann_selected = std::max(0, (int)ann_titles.size() - 1);
    };

    MenuOption menu_opt;
    auto scene_menu = Menu(&scene_titles, &scene_selected, menu_opt);
    MenuOption ann_opt;
    auto ann_menu = Menu(&ann_titles, &ann_selected, ann_opt);

    auto open_btn = Button("Open", [&] { modal_error.clear(); open_shown = true; });
    auto prev_btn = Button("Prev", [&] { if (scene_selected > 0) scene_selected--; });
    auto next_btn = Button("Next", [&] { if (scene_selected + 1 < (int)doc.scenes.size()) scene_selected++; });
    auto do_mark = [&] {
        if (!have_doc) { status = "Open a pass first."; return; }
        viewer_impl->begin_mark();
        viewer_impl->TakeFocus();
        status = "Marking — move the cursor to the end of the range, then Set (Esc cancels).";
    };
    auto mark_btn = Button("Mark", [&] { do_mark(); });
    auto begin_set = [&] {
        if (!have_doc) { status = "Open a pass first."; return; }
        Selection sel = viewer_impl->selection();
        if (!sel.ok) { status = "Mark a range in the scene text first (Mark, then move the cursor)."; return; }
        set_sel = sel;
        set_quote = sel.quote.size() > 60 ? sel.quote.substr(0, 57) + "\u2026" : sel.quote;
        set_note.clear(); set_error.clear();
        set_kind = doc.pass.kinds.empty() ? std::string("Editor") : doc.pass.kinds.front();
        set_shown = true;
    };
    auto set_btn  = Button("Set",  [&] { begin_set(); });
    auto seal_btn = Button("Seal", [&] {
        if (!have_doc) { status = "Open a pass first."; return; }
        try {
            fe::KeyPair kp = load_or_create_identity();
            fe::CustodyEvent e;
            e.kind     = fe::CustodyEvent_Kind::Sealed;
            e.actor    = editor_name.empty() ? std::string("editor") : editor_name;
            e.actor_id = fe::fingerprint(kp.public_key);   // set before finalize (bound)
            e.at       = now_iso();
            e.binds    = fe::annotations_hash(doc);        // over the returned block
            fe::sign_event(fe::append_event(doc.custody, e), kp);
            dirty = true;
            status = "Sealed by " + e.actor + " — now Save to write it back.";
        } catch (const std::exception& ex) {
            status = std::string("Seal failed: ") + ex.what();
        }
    });
    auto save_btn = Button("Save", [&] {
        if (!have_doc) { status = "Open a pass first."; return; }
        try {
            if (!in_pass.empty()) fe::save_document_pw(in_file, doc, in_pass);
            else                  fe::save_document_plain(in_file, doc);
            dirty = false;
            status = "Saved to " + in_file + " — send it back to the author.";
        } catch (const std::exception& ex) {
            status = std::string("Save failed: ") + ex.what();
        }
    });
    auto help_btn = Button("Help", [&] { help_shown = true; });
    auto quit_btn = Button("Quit", [&] { screen.Exit(); });

    // ── the Set modal: type the note for the marked range, file it as a proposal ─
    auto set_file = Button("File note", [&] {
        if (set_note.empty()) { set_error = "Type a note first."; return; }
        if (scene_selected < 0 || scene_selected >= (int)doc.scenes.size()) { set_shown = false; return; }
        fe::Annotation a;
        a.scene_iid   = doc.scenes[(std::size_t)scene_selected].iid;
        a.range_start = set_sel.start;
        a.range_end   = set_sel.end;
        a.quote       = set_sel.quote;
        a.kind        = set_kind;
        a.text        = set_note;
        a.verdict     = fe::Verdict::Proposed;   // a note you write is a proposal to the author
        doc.annotations.push_back(std::move(a));
        viewer_impl->clear_mark();
        dirty = true;
        set_shown = false;
        status = "Note filed as a proposal on \u201c" + doc.scenes[(std::size_t)scene_selected].title + "\u201d.";
    });
    auto set_cancel = Button("Cancel", [&] { set_shown = false; });
    auto set_container = Container::Vertical({note_input, Container::Horizontal({set_file, set_cancel})});
    auto set_modal = Renderer(set_container, [&] {
        return vbox({
                   text("Write a note") | bold,
                   separator(),
                   hbox({text("Range  ") | dim, text("\u201c" + set_quote + "\u201d") | color(Color::Plum1)}),
                   hbox({text("Hat    ") | dim, text(set_kind)}),
                   text(""),
                   hbox({text("Note   ") | dim, note_input->Render() | flex | border}),
                   (set_error.empty() ? text("") : text(set_error) | color(Color::Red)),
                   separator(),
                   hbox({text("It never rewrites the prose \u2014 the author accepts or declines it.") | dim,
                         filler(), set_file->Render(), text("  "), set_cancel->Render()}),
               }) | border | size(WIDTH, GREATER_THAN, 64) | bgcolor(Color::Black);
    });
    auto verb_bar = Container::Horizontal({
        open_btn, prev_btn, next_btn, mark_btn, set_btn, seal_btn, save_btn, help_btn, quit_btn,
    });

    auto main_container = Container::Vertical({
        Container::Horizontal({scene_menu, viewer, ann_menu}), verb_bar,
    });

    auto accent = [](bool active) -> Color {
        return active ? Color(Color::Plum1) : Color(Color::GrayDark);
    };

    auto main_renderer = Renderer(main_container, [&] {
        rebuild_ann_list();   // keep the right panel in sync with the current scene

        auto header = hbox({
            text("folioedit") | color(Color::Plum1),
            text("   pass: ") | color(Color::GrayDark),
            (have_doc ? text(doc.project_title) : text("none open") | dim),
        });

        Element left = have_doc ? (scene_menu->Render() | vscroll_indicator | yframe)
                                : vbox({filler(), text("(no pass open)") | dim, filler()});
        left = left | size(WIDTH, EQUAL, 24)
                    | borderStyled(accent(scene_menu->Focused()));

        Element view = viewer->Render() | flex | borderStyled(accent(viewer->Focused()));

        // Right panel: this scene's notes (glyph · range · quote · abbrev text).
        Element rlist = ann_titles.empty()
            ? vbox({filler(), text("no notes yet") | dim, filler()})
            : (ann_menu->Render() | vscroll_indicator | yframe);
        Element right = vbox({text(" Notes") | color(Color::GrayDark), separator(), rlist})
                        | size(WIDTH, EQUAL, 32)
                        | borderStyled(accent(ann_menu->Focused()));

        auto top = hbox({left, view, right}) | flex;

        // Annotation detail: the selected note in full, under the text.
        Element anno_body;
        if (!have_doc) {
            anno_body = text("Open a pass to begin.") | dim;
        } else if (viewer_impl->marking()) {
            anno_body = text("Marking — move the cursor to the end of the range, then Set (s). Esc cancels.") | dim;
        } else if (!ann_index.empty() && ann_selected >= 0 && ann_selected < (int)ann_index.size()) {
            const fe::Annotation& a = doc.annotations[(std::size_t)ann_index[(std::size_t)ann_selected]];
            const char* word = "Proposed"; Color vc = Color::Plum1; const char* g = "\u25B8";
            if (a.verdict == fe::Verdict::Accepted) { word = "Accepted"; vc = Color::RGB(0xa6,0xe3,0xa1); g = "\u2713"; }
            else if (a.verdict == fe::Verdict::Declined) { word = "Declined"; vc = Color::RGB(0xf3,0x8b,0xa8); g = "\u2717"; }
            anno_body = vbox({
                hbox({text(std::string(g) + " " + word) | color(vc),
                      text("   " + (a.kind.empty() ? std::string("note") : a.kind)) | color(Color::Plum1),
                      text("   " + std::to_string(a.range_start) + "\u2013" + std::to_string(a.range_end)) | dim,
                      filler()}),
                text("\u201c" + a.quote + "\u201d") | dim,
                paragraph(a.text),
            });
        } else {
            anno_body = text("Mark (m) a range in the scene, then Set (s) to write a note.") | dim;
        }
        auto anno = vbox({text("Annotation") | color(Color::GrayDark), anno_body})
                    | border | size(HEIGHT, EQUAL, 6);

        auto bar = hbox({
            open_btn->Render(), text(" "), prev_btn->Render(), text(" "), next_btn->Render(), text("   "),
            mark_btn->Render(), text(" "), set_btn->Render(), text(" "),
            seal_btn->Render(), text(" "), save_btn->Render(), filler(),
            help_btn->Render(), text(" "), quit_btn->Render(),
        }) | borderStyled(accent(verb_bar->Focused()));

        auto statusline = hbox({
            text(status) | dim,
            filler(),
            (dirty ? text("\u25CF unsaved — Seal, then Save") | color(Color::RGB(0xf3, 0x8b, 0xa8))
                   : text("")),
        });
        return vbox({header, top, anno, bar, statusline}) | border;
    });

    auto app = Modal(main_renderer, open_modal, &open_shown);
    app = Modal(app, help_modal, &help_shown);
    app = Modal(app, set_modal, &set_shown);
    app = CatchEvent(app, [&](Event e) {
        const bool modal = open_shown || help_shown || set_shown;
        if (!modal && (e == Event::Character('?') || e == Event::F1)) { help_shown = true; return true; }
        // Quick verbs while reading the scene (not while a modal or a text field owns keys).
        if (!modal && viewer_impl->Focused()) {
            if (e == Event::Character('m')) { do_mark(); return true; }
            if (e == Event::Character('s')) { begin_set(); return true; }
        }
        if (e == Event::Escape) {
            if (set_shown)  { set_shown = false; return true; }
            if (help_shown) { help_shown = false; return true; }
            if (open_shown) { open_shown = false; return true; }
            if (viewer_impl->marking()) { viewer_impl->clear_mark(); status = "Mark cleared."; return true; }
        }
        return false;
    });

    screen.Loop(app);
    return 0;
}

}  // namespace folioedit
