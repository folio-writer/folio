// ─────────────────────────────────────────────────────────────────────────────
// TEST_resolution.cpp — s108 §25: two-key Resolve handshake + §21.2 send back.
//
// Proves the pure engine face:
//   1. resolution_state() — the §25.3 rule as a truth table over event logs:
//      two-key close, author-authoritative reopen, editor-reopen-withdraws-own-half,
//      latest-by-timestamp. resolution_resolved == (state==Resolved).
//   2. JSON round-trip + omit-when-empty + garbage tolerance (§21.6).
//   3. annotations_hash binding (empty log == pre-§22 hash; an event changes it).
//   4. make_sendback_document (§21.2/§25.5) — current scenes travel, TWO authored
//      links appended (issued body_v2 + sealed annotations), body_v2 != body_v1.
//
// Pure (FOLIOEDIT_NO_CRYPTO) -> sandbox-provable under the full strict flags.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -DFOLIOEDIT_NO_CRYPTO -I /home/claude/sbox -I ../include TEST_resolution.cpp ../src/Format.cpp ../src/Archive.cpp ../src/Custody.cpp ../src/Sha256.cpp -o /tmp/test_resolution && /tmp/test_resolution
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -DFOLIOEDIT_NO_CRYPTO -I ../include TEST_resolution.cpp ../src/Format.cpp ../src/Archive.cpp ../src/Custody.cpp ../src/Sha256.cpp -o /tmp/test_resolution && /tmp/test_resolution
*/
#include "folioedit/Format.hpp"
#include "folioedit/Archive.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace folioedit;

static int g_pass = 0, g_fail = 0;
static void ok(bool c, const std::string& what) {
    if (c) ++g_pass; else { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}
static ResolutionEvent ev(Resolver by, bool resolved, const std::string& at) {
    return ResolutionEvent{by, resolved, at};
}
static const char* name(ResolutionState s) {
    switch (s) { case ResolutionState::Open: return "Open";
                 case ResolutionState::HalfAuthor: return "HalfAuthor";
                 case ResolutionState::HalfEditor: return "HalfEditor";
                 case ResolutionState::Resolved: return "Resolved"; }
    return "?";
}
static void st(const std::vector<ResolutionEvent>& log, ResolutionState want,
               const std::string& what) {
    ResolutionState got = resolution_state(log);
    if (got == want) ++g_pass;
    else { ++g_fail; std::cout << "  FAIL: " << what << " (got " << name(got)
                               << ", want " << name(want) << ")\n"; }
}

int main() {
    const Resolver A = Resolver::Author, E = Resolver::Editor;
    using S = ResolutionState;

    // ── 1. §25.3 handshake truth table ─────────────────────────────────────────
    st({}, S::Open, "empty -> Open");
    st({ev(A,true,"t1")}, S::HalfAuthor, "author resolves -> HalfAuthor (waiting on editor)");
    st({ev(E,true,"t1")}, S::HalfEditor, "editor resolves -> HalfEditor (author's turn)");
    st({ev(A,true,"t1"), ev(E,true,"t2")}, S::Resolved, "both resolve -> Resolved");
    st({ev(E,true,"t1"), ev(A,true,"t2")}, S::Resolved, "both resolve (other order) -> Resolved");

    // author reopen is authoritative -- clears the editor's half too
    st({ev(A,true,"t1"), ev(A,false,"t2")}, S::Open, "author resolve then reopen -> Open");
    st({ev(A,true,"t1"), ev(E,true,"t2"), ev(A,false,"t3")}, S::Open,
       "author reopen clears a FULL resolve (authoritative)");
    st({ev(E,true,"t1"), ev(A,false,"t2")}, S::Open,
       "editor resolved, author reopens -> Open (override)");
    st({ev(A,false,"t1")}, S::Open, "lone author reopen -> Open");

    // editor reopen withdraws ONLY the editor's half; cannot force open vs author
    st({ev(A,true,"t1"), ev(E,true,"t2"), ev(E,false,"t3")}, S::HalfAuthor,
       "editor reopen withdraws own half -> back to HalfAuthor");
    st({ev(E,true,"t1"), ev(E,false,"t2")}, S::Open, "editor resolve then reopen -> Open");
    st({ev(A,true,"t1"), ev(E,false,"t2")}, S::HalfAuthor,
       "editor reopen (no prior editor resolve) leaves author's half intact");

    // latest per side is by timestamp
    st({ev(A,true,"t9"), ev(A,false,"t1")}, S::HalfAuthor,
       "author latest by TIMESTAMP (t9 resolve beats earlier t1 reopen)");
    st({ev(A,false,"t9"), ev(A,true,"t1")}, S::Open,
       "author latest reopen (t9) beats earlier resolve -> Open");

    // convenience: only full resolve counts as resolved()
    ok(!resolution_resolved({ev(A,true,"t1")}), "resolved()==false at HalfAuthor");
    ok(!resolution_resolved({ev(E,true,"t1")}), "resolved()==false at HalfEditor");
    ok(resolution_resolved({ev(A,true,"t1"), ev(E,true,"t2")}), "resolved()==true when both");

    // ── 2. JSON round-trip + omit-when-empty + tolerance ───────────────────────
    {
        Document d; Annotation a; a.scene_iid = "scn_1"; a.kind = "Editor"; a.text = "x";
        a.resolution_log = {ev(E,true,"2026-07-03T10:00:00Z"), ev(A,true,"2026-07-03T11:00:00Z")};
        d.annotations.push_back(a);
        Document back = from_json(to_json(d));
        const auto& rl = back.annotations.at(0).resolution_log;
        ok(rl.size() == 2, "round-trip: two events survive");
        ok(resolution_state(rl) == S::Resolved, "round-trip: effective state preserved (Resolved)");
    }
    {
        Document d; Annotation a; a.scene_iid = "s"; d.annotations.push_back(a);
        ok(!to_json(d)["annotations"][0].contains("resolution"), "empty log omits key");
    }
    {
        json j = json::parse(R"({"annotations":[{"scene_iid":"s","resolution":"bad"},
                                                 {"scene_iid":"s","resolution":[{"by":"martian"}]}]})");
        Document d;
        try { d = from_json(j); ok(true, "garbled resolution: no throw"); }
        catch (...) { ok(false, "garbled resolution THREW"); }
        ok(d.annotations.at(0).resolution_log.empty(), "non-array -> empty log");
        ok(d.annotations.at(1).resolution_log.size()==1 &&
           d.annotations.at(1).resolution_log[0].by==E, "unknown actor -> Editor, no throw");
    }

    // ── 3. annotations_hash omit-when-empty invariant ──────────────────────────
    {
        Document d; Annotation a; a.scene_iid="h"; a.verdict=Verdict::Accepted;
        d.annotations.push_back(a);
        const std::string h0 = annotations_hash(d);
        d.annotations[0].resolution_log = {ev(A,true,"2026-07-03T12:00:00Z")};
        ok(annotations_hash(d) != h0, "a resolution event changes the hash");
        d.annotations[0].resolution_log.clear();
        ok(annotations_hash(d) == h0, "clearing restores the pre-§22 hash");
    }

    // ── 4. make_sendback_document (§21.2/§25.5) ────────────────────────────────
    {
        // A carrier `sent`: one scene (body_v1) + an issued link binding it.
        Document sent;
        sent.project_title = "Book";
        sent.scenes.push_back(Scene{"scn_1", "", 0, "The old sentence."});
        CustodyEvent iss0; iss0.kind = CustodyEvent_Kind::Issued;
        iss0.actor = "author"; iss0.at = "t0"; iss0.binds = body_hash(sent);
        append_event(sent.custody, iss0);
        const std::size_t custody0 = sent.custody.size();
        const std::string body_v1 = body_hash(sent);

        // Current (revised) scenes -- the fix -- + a verdicted note.
        std::vector<Scene> current = { Scene{"scn_1", "", 0, "The revised sentence."} };
        std::vector<Annotation> returned;
        Annotation ra; ra.scene_iid="scn_1"; ra.kind="Editor"; ra.text="tighten";
        ra.verdict = Verdict::Accepted; returned.push_back(ra);

        Document sb = make_sendback_document(sent, current, returned, "author", "fp", "t1");

        ok(sb.scenes.size()==1 && sb.scenes[0].text=="The revised sentence.",
           "sendback: CURRENT (revised) prose travels");
        ok(sb.custody.size() == custody0 + 2, "sendback: TWO authored links appended");
        ok(sb.custody[custody0].kind   == CustodyEvent_Kind::Issued, "link 1 is Issued (re-issue body)");
        ok(sb.custody[custody0+1].kind == CustodyEvent_Kind::Sealed, "link 2 is Sealed (annotations)");
        ok(sb.custody[custody0].binds  == body_hash(sb), "issued link binds body_v2");
        ok(sb.custody[custody0+1].binds == annotations_hash(sb), "sealed link binds annotations");
        ok(body_hash(sb) != body_v1, "body_v2 != body_v1 -> change indicator is real");

        // make_return_document still carries the OLD body (regression: verdict-only path).
        Document ret = make_return_document(sent, returned, "author", "fp", "t1");
        ok(ret.scenes[0].text == "The old sentence.", "make_return_document still carries old body");
    }

    std::cout << "\nresolution+sendback (engine): " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
