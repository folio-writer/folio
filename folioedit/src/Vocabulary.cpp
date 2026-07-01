//
// folioedit :: Vocabulary -- the command/kind data and its renderers. Pure STL.
//
#include "folioedit/Vocabulary.hpp"

namespace folioedit {

const std::vector<Command>& commands() {
    static const std::vector<Command> table = {
        { "help", "Show this help, or help for one command",
          { { "command", false, "a command to describe in detail" } } },
        { "keygen", "Generate an Ed25519 editor identity keypair",
          { { "keyfile", true, "path to write the new key (hex; keep the private half secret)" } } },
        { "info", "Inspect a .folioedit file; with a key, verify custody + list events",
          { { "file",   true,  "the .folioedit file" },
            { "--key",  false, "64-char hex key (for a raw-key file)" },
            { "--pass", false, "passphrase (for a passphrase file)" } } },
        { "annotate", "Add an annotation to a file (a proposal; clay until sealed)",
          { { "file",     true,  "the .folioedit file" },
            { "--key/--pass", true, "the key or passphrase that opens the file" },
            { "--scene",  true,  "scene iid the note lands on" },
            { "--start",  false, "range start (char offset in the scene)" },
            { "--end",    false, "range end" },
            { "--quote",  false, "the exact spanned text (re-anchor fallback)" },
            { "--kind",   true,  "the hat (must be one the pass allows)" },
            { "--text",   true,  "the annotation text" } } },
        { "del", "Withdraw an annotation -- a tombstone, kept in the record, not erased",
          { { "file",     true,  "the .folioedit file" },
            { "--key/--pass", true, "the key or passphrase that opens the file" },
            { "--index",  true,  "the annotation index (see `info`)" } } },
        { "seal", "Append a signed `sealed` custody event committing the current annotations",
          { { "file",       true,  "the .folioedit file" },
            { "--key/--pass", true, "the key or passphrase that opens the file" },
            { "--identity", true,  "editor key file (from `keygen`) that signs the event" },
            { "--actor",    true,  "human label for the editor (e.g. jane)" } } },
    };
    return table;
}

const std::vector<Kind>& kinds() {
    // The hats a pass may wear (a pass declares its allowed subset; each
    // annotation picks one from that subset).
    static const std::vector<Kind> table = {
        { "Proofreader", "mechanics only -- spelling, punctuation, grammar" },
        { "Editor",      "line + continuity against the object graph; never rewrites prose" },
        { "Writer",      "developmental notes -- structure, pacing, arc" },
    };
    return table;
}

const Command* find_command(const std::string& name) {
    for (const Command& c : commands())
        if (c.name == name) return &c;
    return nullptr;
}

std::string render_command_help(const Command& c) {
    std::string out = "  folioedit " + c.name;
    for (const CommandArg& a : c.args)
        out += a.required ? (" <" + a.name + ">") : (" [" + a.name + "]");
    out += "\n      " + c.summary + "\n";
    for (const CommandArg& a : c.args)
        out += "        " + a.name + (a.required ? "  " : "  (optional) ") + " -- " + a.help + "\n";
    return out;
}

std::string render_help() {
    std::string out = "folioedit -- editorial-interchange engine\n\nCommands:\n";
    for (const Command& c : commands())
        out += render_command_help(c) + "\n";
    out += "Annotation kinds (hats):\n";
    for (const Kind& k : kinds())
        out += "  " + k.name + " -- " + k.summary + "\n";
    return out;
}

}  // namespace folioedit
