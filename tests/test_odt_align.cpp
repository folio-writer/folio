// ─────────────────────────────────────────────────────────────────────────────
// test_odt_align.cpp — ODT paragraph-alignment import (s89)
//
// WHAT THIS PROVES
//   1. odt_collect_align_styles: scanning an ODT styles.xml/content.xml for
//      fo:text-align inside <style:style> blocks maps each style name to the
//      editor's text-align token — center, right (from end/right), justify —
//      and OMITS the defaults (start/left), so an unaligned paragraph stays a
//      plain <p>.
//   2. The emission format wraps an aligned paragraph in the editor's canonical
//      alignment span, <span style="text-align:X">…</span>, OUTERMOST of any
//      bold/italic, so the whole paragraph range round-trips to a justify_* tag.
//
// WHY A MIRRORED COPY
//   odt_collect_align_styles is static in Importer.cpp (no test seam) and the
//   importer TU pulls zlib/dirent. The collector + emission below are a verbatim
//   mirror of the shipping logic; keep them in sync with Importer.cpp.
// ─────────────────────────────────────────────────────────────────────────────
/*
  BUILD + RUN
  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -o /tmp/test_odt_align \
        tests/test_odt_align.cpp && /tmp/test_odt_align
  Fedora (clang++ 21, project flags):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        -o /tmp/test_odt_align tests/test_odt_align.cpp && /tmp/test_odt_align
*/

#include <cstdio>
#include <map>
#include <string>

// ── Mirror of Importer.cpp :: odt_collect_align_styles ───────────────────────
static void odt_collect_align_styles(const std::string& xml,
                                     std::map<std::string, std::string>& align) {
    size_t pos = 0;
    while (true) {
        size_t s = xml.find("<style:style", pos);
        if (s == std::string::npos) break;
        size_t hdr_end = xml.find('>', s);
        if (hdr_end == std::string::npos) break;
        size_t e = xml.find("</style:style>", s);

        std::string open = xml.substr(s, hdr_end - s);
        std::string name;
        size_t np = open.find("style:name=\"");
        if (np != std::string::npos) {
            np += 12;
            size_t nq = open.find('"', np);
            if (nq != std::string::npos) name = open.substr(np, nq - np);
        }
        std::string block = (e == std::string::npos) ? open : xml.substr(s, e - s);
        if (!name.empty()) {
            size_t ap = block.find("fo:text-align=\"");
            if (ap != std::string::npos) {
                ap += 15;
                size_t aq = block.find('"', ap);
                if (aq != std::string::npos) {
                    std::string v = block.substr(ap, aq - ap);
                    std::string mapped;
                    if      (v == "center")               mapped = "center";
                    else if (v == "end" || v == "right")  mapped = "right";
                    else if (v == "justify")              mapped = "justify";
                    if (!mapped.empty()) align[name] = mapped;
                }
            }
        }
        pos = (e == std::string::npos) ? hdr_end + 1 : e + 14;
    }
}

// ── Mirror of the /text:p emission (alignment + b/i, align outermost) ─────────
static std::string emit_para(const std::string& inner, const std::string& align,
                             bool bold, bool italic) {
    std::string open, close;
    if (!align.empty()) { open += "<span style=\"text-align:" + align + "\">"; close = "</span>" + close; }
    if (bold)   { open += "<b>"; close = "</b>" + close; }
    if (italic) { open += "<i>"; close = "</i>" + close; }
    return "<p>" + open + inner + close + "</p>";
}

static int g_pass = 0, g_fail = 0;
static void eq(const char* what, const std::string& got, const std::string& want) {
    if (got == want) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL %s:\n    got  [%s]\n    want [%s]\n",
                                 what, got.c_str(), want.c_str()); }
}
static void has(const char* what, const std::map<std::string,std::string>& m,
                const std::string& k, const std::string& v) {
    auto it = m.find(k);
    std::string got = (it == m.end()) ? std::string("<none>") : it->second;
    eq(what, got, v);
}

int main() {
    // A styles block exercising every alignment value + a no-align style.
    const std::string styles =
        "<office:automatic-styles>"
        "<style:style style:name=\"P1\" style:family=\"paragraph\">"
        "  <style:paragraph-properties fo:text-align=\"center\"/></style:style>"
        "<style:style style:name=\"P2\" style:family=\"paragraph\">"
        "  <style:paragraph-properties fo:text-align=\"end\"/></style:style>"
        "<style:style style:name=\"P3\" style:family=\"paragraph\">"
        "  <style:paragraph-properties fo:text-align=\"justify\"/></style:style>"
        "<style:style style:name=\"P4\" style:family=\"paragraph\">"
        "  <style:paragraph-properties fo:text-align=\"start\"/></style:style>"
        "<style:style style:name=\"P5\" style:family=\"paragraph\">"
        "  <style:paragraph-properties fo:text-align=\"right\"/></style:style>"
        "<style:style style:name=\"P6\" style:family=\"paragraph\">"
        "  <style:text-properties fo:font-weight=\"bold\"/></style:style>"
        "</office:automatic-styles>";

    std::map<std::string, std::string> align;
    odt_collect_align_styles(styles, align);

    has("P1 center",  align, "P1", "center");
    has("P2 end→right", align, "P2", "right");
    has("P3 justify", align, "P3", "justify");
    eq ("P4 start omitted", align.count("P4") ? align["P4"] : "<none>", "<none>");
    has("P5 right",   align, "P5", "right");
    eq ("P6 no-align omitted", align.count("P6") ? align["P6"] : "<none>", "<none>");
    eq ("count is 4", std::to_string(align.size()), "4");

    // Emission: aligned wraps in span; default stays plain; align outermost of b/i.
    eq("plain",            emit_para("hi", "",        false, false), "<p>hi</p>");
    eq("center",           emit_para("hi", "center",  false, false),
       "<p><span style=\"text-align:center\">hi</span></p>");
    eq("right + bold",     emit_para("hi", "right",   true,  false),
       "<p><span style=\"text-align:right\"><b>hi</b></span></p>");
    eq("justify + b + i",  emit_para("hi", "justify", true,  true),
       "<p><span style=\"text-align:justify\"><b><i>hi</i></b></span></p>");
    eq("bold only no align", emit_para("hi", "",      true,  false), "<p><b>hi</b></p>");

    std::printf("\n%s: %d passed, %d failed\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
