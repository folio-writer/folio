#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// folioedit — Tui.hpp   (the interactive human face; FTXUI, GUI-like in a terminal)
//
// Bare `folioedit` (no args) launches this. It comes up as an empty paned app —
// scene list, manuscript viewer, annotation pane, verb bar — with a short
// instruction in the viewer. The editor clicks Open, enters the file, the
// four-word passphrase, and their name; the panes then fill and the scenes become
// readable and navigable. Editing verbs (Mark/Set/Save/Seal) arrive in slice 2;
// this slice lands the shell + the Open→load→browse flow.
//
// The passphrase is typed at an in-app field, never a command-line flag, so the
// shell-quoting problem never arises (this was the whole point of the interactive
// face, per DESIGN_editorialization §9.3 / the s102 decision).
//
// Requires the engine (Archive/Format) — so it lives in the sealed build. Gated by
// -DFOLIOEDIT_BUILD_TUI=ON (which also pulls FTXUI via FetchContent).
// ─────────────────────────────────────────────────────────────────────────────
#include <string>

namespace folioedit {

// Run the interactive TUI. If `initial_file` is non-empty the Open dialog opens
// pre-filled with it. Returns a process exit code.
int run_tui(const std::string& initial_file = "");

}  // namespace folioedit
