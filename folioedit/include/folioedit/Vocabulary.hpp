#pragma once
//
// folioedit :: Vocabulary -- the ONE command + kind vocabulary, as DATA.
//
// The insight (s100 handoff): a face's `:help`, the CLI's `--help`, and the AI's
// embedded rules are the SAME vocabulary rendered three ways. So it must live as
// data in the engine, not be hardcoded per face -- one source of truth, three
// renderers. This is that source. Pure STL: no crypto, no gtk, no json.
//
// Only settled, read-side commands live here so far (help / keygen / info). The
// mutate verbs (annotate / seal / del) join once their custody semantics are
// decided -- keeping the "spec" honest about what actually exists.
//
#include <string>
#include <vector>

namespace folioedit {

struct CommandArg {
    std::string name;       // "file", "--key", "--pass"
    bool        required = false;
    std::string help;
};

struct Command {
    std::string             name;      // the verb, e.g. "info"
    std::string             summary;   // one line
    std::vector<CommandArg> args;
};

struct Kind {                          // an annotation "hat"
    std::string name;                  // "Proofreader" / "Editor" / "Writer"
    std::string summary;
};

// The data.
const std::vector<Command>& commands();
const std::vector<Kind>&    kinds();
const Command*              find_command(const std::string& name);  // nullptr if unknown

// Renderers over that data (the "three faces" share these).
std::string render_help();                          // the full --help / :help text
std::string render_command_help(const Command& c);  // one command's usage block

}  // namespace folioedit
