// ─────────────────────────────────────────────────────────────────────────────
// TEST_resolution_boundary.cpp — s108 §22 engine<->model contract tripwire.
//
// The model mirrors the engine's resolver words as string constants (Resolvers::)
// rather than linking folioedit, exactly as s107 did for Verdicts::. This asserts
// the two never drift: folioedit::resolver_to_str(...) MUST equal Resolvers::,
// both directions. If someone renames "author"/"editor" on one side, this fails.
//
// FEDORA-ONLY: including DocumentModel.hpp transitively pulls glibmm (giomm), so
// this cannot build in the g++ sandbox (see DocumentModel.hpp's own note). The
// engine half of §22 -- the precedence truth table, JSON, and hash binding -- is
// sandbox-proven in TEST_resolution.cpp; this closes the contract on Fedora.
// The model's resolution_resolved() is a line-for-line mirror of the engine rule
// that TEST_resolution.cpp exercises 24/24; a full model-side precedence test is
// cheap to add once the GTK wiring links DocumentModel anyway.
// ─────────────────────────────────────────────────────────────────────────────
/*
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow $(pkg-config --cflags gtkmm-4.0) -I ../include -I ../folioedit/include TEST_resolution_boundary.cpp ../folioedit/src/Format.cpp $(pkg-config --libs gtkmm-4.0) -o /tmp/test_resolution_boundary && /tmp/test_resolution_boundary
*/
#include "DocumentModel.hpp"          // Folio::Resolvers::
#include "folioedit/Format.hpp"       // folioedit::resolver_to_str / _from_str

#include <iostream>
#include <string>

int main() {
    using folioedit::Resolver;
    int fail = 0;
    auto eq = [&](const std::string& a, const std::string& b, const char* what) {
        if (a != b) { ++fail; std::cout << "  FAIL: " << what
                                        << " (\"" << a << "\" != \"" << b << "\")\n"; }
    };

    // engine word  ==  model constant, both directions
    eq(folioedit::resolver_to_str(Resolver::Author), Folio::Resolvers::kAuthor, "author word");
    eq(folioedit::resolver_to_str(Resolver::Editor), Folio::Resolvers::kEditor, "editor word");

    if (folioedit::resolver_from_str(Folio::Resolvers::kAuthor) != Resolver::Author)
        { ++fail; std::cout << "  FAIL: kAuthor does not parse back to Author\n"; }
    if (folioedit::resolver_from_str(Folio::Resolvers::kEditor) != Resolver::Editor)
        { ++fail; std::cout << "  FAIL: kEditor does not parse back to Editor\n"; }

    std::cout << (fail ? "resolution boundary: FAILED\n" : "resolution boundary: OK\n");
    return fail ? 1 : 0;
}
