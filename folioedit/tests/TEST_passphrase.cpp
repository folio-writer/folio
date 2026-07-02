//
// TEST_passphrase.cpp -- folioedit :: Passphrase (pure generator + canonicalizer)
//
// Proves the seam that killed the "forgot to quote" failure: however a phrase is
// typed -- quoted, unquoted-then-rejoined, hyphenated, underscored, mixed case,
// double-spaced, padded -- canonicalize_passphrase folds it to ONE string, so the
// derived key is identical. Also checks the generator: deterministic under an
// injected RNG, all-lowercase single-space display form, and that a generated
// phrase survives canonicalize -> the form the KDF actually sees.
//
// Pure STL -- compiles + runs in the sandbox with strict flags, no OpenSSL.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_passphrase.cpp src/Passphrase.cpp -o /tmp/test_passphrase && /tmp/test_passphrase
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I include tests/TEST_passphrase.cpp src/Passphrase.cpp -o /tmp/test_passphrase && /tmp/test_passphrase
*/

#include "folioedit/Passphrase.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace fe = folioedit;

static int g_pass = 0, g_fail = 0;
static void check(const std::string& what, bool cond) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

int main() {
    // ── canonicalize: the whole point ────────────────────────────────────────
    const std::string canon = "ottersunionizediscountturnips";

    check("spaced form folds",       fe::canonicalize_passphrase("otters unionize discount turnips") == canon);
    check("hyphenated form folds",   fe::canonicalize_passphrase("otters-unionize-discount-turnips") == canon);
    check("underscored form folds",  fe::canonicalize_passphrase("otters_unionize_discount_turnips") == canon);
    check("mixed case folds",        fe::canonicalize_passphrase("Otters Unionize Discount Turnips") == canon);
    check("SHOUTING folds",          fe::canonicalize_passphrase("OTTERS UNIONIZE DISCOUNT TURNIPS") == canon);
    check("double spaces fold",      fe::canonicalize_passphrase("otters  unionize   discount turnips") == canon);
    check("leading/trailing pad",    fe::canonicalize_passphrase("   otters unionize discount turnips  ") == canon);
    check("tabs + newline fold",     fe::canonicalize_passphrase("otters\tunionize\ndiscount\r\nturnips") == canon);
    check("already-canonical fixed", fe::canonicalize_passphrase(canon) == canon);
    check("idempotent",              fe::canonicalize_passphrase(fe::canonicalize_passphrase("Otters-Unionize")) ==
                                     fe::canonicalize_passphrase("Otters-Unionize"));

    // The single most important property: every plausible way the editor might
    // type the SAME phrase yields the SAME key input.
    {
        const std::vector<std::string> typed = {
            "otters unionize discount turnips",
            "otters-unionize-discount-turnips",
            "  Otters  Unionize  Discount  Turnips  ",
            "OTTERS_UNIONIZE_DISCOUNT_TURNIPS",
            "ottersunionizediscountturnips",
        };
        bool all_same = true;
        for (const auto& t : typed)
            if (fe::canonicalize_passphrase(t) != canon) all_same = false;
        check("all typings derive one key input", all_same);
    }

    // A genuinely different phrase must NOT collide (canonicalization is lossy on
    // spacing/case only -- the words still matter).
    check("different words differ",
          fe::canonicalize_passphrase("otters unionize discount turnips") !=
          fe::canonicalize_passphrase("otters unionize discount parsnips"));

    // Non-ASCII bytes pass through (pools are ASCII, but a human might type them).
    check("non-ascii preserved, ascii lowered",
          fe::canonicalize_passphrase("Cafe\xC3\xA9 Beans") == "cafe\xC3\xA9" "beans");

    // ── generator: deterministic under an injected RNG ───────────────────────
    {
        // pick(n) -> 0 always: first word of every pool.
        fe::IndexRng zero = [](std::size_t) -> std::size_t { return 0; };
        std::string p = fe::generate_passphrase(zero);
        const std::string expect = fe::pool(fe::Slot::Subject)[0]   + " " +
                                   fe::pool(fe::Slot::Verb)[0]      + " " +
                                   fe::pool(fe::Slot::Adjective)[0] + " " +
                                   fe::pool(fe::Slot::Object)[0];
        check("generator draws one word per slot, in order", p == expect);
    }
    {
        // A different fixed index per call still lands a valid four-word phrase.
        fe::IndexRng two = [](std::size_t n) -> std::size_t { return 2 % n; };
        std::string p = fe::generate_passphrase(two);
        // exactly three separating spaces -> four words
        std::size_t spaces = 0;
        for (char c : p) if (c == ' ') ++spaces;
        check("generated phrase has four words", spaces == 3);
    }

    // Generated phrases are lowercase ASCII with single-space joins, so the
    // display form and the canonical form differ only by the spaces.
    {
        fe::IndexRng one = [](std::size_t n) -> std::size_t { return 1 % n; };
        std::string p = fe::generate_passphrase(one);
        bool clean = true;
        for (char c : p)
            if (!((c >= 'a' && c <= 'z') || c == ' ')) clean = false;
        check("generated phrase is lowercase + spaces only", clean);
        std::string despaced;
        for (char c : p) if (c != ' ') despaced.push_back(c);
        check("canonical(generated) just drops the spaces",
              fe::canonicalize_passphrase(p) == despaced);
    }

    // ── entropy floor is reported and non-trivial ────────────────────────────
    check("entropy bits are positive", fe::phrase_entropy_bits() > 0.0);
    std::cout << "  [info] pool sizes: subject=" << fe::pool(fe::Slot::Subject).size()
              << " verb=" << fe::pool(fe::Slot::Verb).size()
              << " adjective=" << fe::pool(fe::Slot::Adjective).size()
              << " object=" << fe::pool(fe::Slot::Object).size()
              << " -> " << fe::phrase_entropy_bits() << " bits/phrase\n";

    std::cout << "TEST_passphrase: " << g_pass << "/" << (g_pass + g_fail)
              << " passed\n";
    return g_fail == 0 ? 0 : 1;
}
