// folioedit :: Vocabulary tests -- the command/kind data and its renderers.
// PURE: links only Vocabulary.cpp, no libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_vocab.cpp ../src/Vocabulary.cpp -o test_vocab && ./test_vocab
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_vocab.cpp ../src/Vocabulary.cpp -o test_vocab && ./test_vocab
*/

#include "folioedit/Vocabulary.hpp"

#include <cstdio>
#include <string>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}
static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    std::printf("folioedit Vocabulary tests\n");

    check("commands() is non-empty", !fe::commands().empty());
    check("find_command('info') resolves",   fe::find_command("info")   != nullptr);
    check("find_command('keygen') resolves", fe::find_command("keygen") != nullptr);
    check("find_command('nope') is null",    fe::find_command("nope")   == nullptr);

    check("kinds() lists the three hats", fe::kinds().size() == 3);
    check("kinds include Proofreader/Editor/Writer",
          fe::kinds()[0].name == "Proofreader" &&
          fe::kinds()[1].name == "Editor" &&
          fe::kinds()[2].name == "Writer");

    const std::string help = fe::render_help();
    check("help renders every command name",
          has(help, "help") && has(help, "keygen") && has(help, "info"));
    check("help renders every kind name",
          has(help, "Proofreader") && has(help, "Editor") && has(help, "Writer"));

    const fe::Command* info = fe::find_command("info");
    const std::string ch = fe::render_command_help(*info);
    check("command help shows the required <file> arg", has(ch, "<file>"));
    check("command help shows an optional [--key]", has(ch, "[--key]"));

    std::printf("\nfolioedit vocab: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
