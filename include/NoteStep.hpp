#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// NoteStep.hpp — s108 §26. The note "stepper": a pure, derived FACE over data
// §20/§25 already track (verdict + resolution state + how many times the pass has
// been sent back). It answers, for one note, WHERE it is in the journey and WHAT
// TO DO NEXT — so the card tells the writer instead of the writer holding the map.
//
// The shape (§26.1): INITIATE -> LOOP (spins N rounds) -> RESOLVE -> END. Not a
// fixed ladder, so we show a PHASE + a next-action line, never "step N of 6". The
// next-action line is imperative (§26.2): it names the move, not the past.
//
// Pure: depends only on <string> and folioedit::ResolutionState (a pure enum), so
// it is sandbox-testable as a truth table. No new stored state, no wire/format
// change — this is a rendering of existing facts.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include "folioedit/Format.hpp"   // folioedit::ResolutionState (pure)

namespace Folio {

struct NoteStep {
    enum class Phase { Proposed, InLoop, Resolving, Done };
    Phase       phase = Phase::Proposed;
    std::string badge;        // "Proposed" | "In the loop" (+ " · round N") | "Resolving" | "Done"
    std::string next_action;  // imperative next move; "" when Done (off the list)
};

// Derive the step for one note.
//   verdict : "" | "proposed" | "accepted" | "declined"   (§20; on-disk word)
//   rs      : the §25 two-key resolution state
//   laps    : how many times this pass has been sent back (0 => no "round N" shown)
// Resolution takes precedence once a key has turned; otherwise the verdict places
// the note in INITIATE (proposed) or the LOOP (acknowledged/declined). The word
// "Acknowledged" is shown for the `accepted` verdict (§26.5) — on-disk stays
// `accepted`. (Folio::ResolutionState mirrors folioedit::ResolutionState value-for-
// value, so a caller holding the model enum passes
// static_cast<folioedit::ResolutionState>(static_cast<int>(model_state)).)
NoteStep note_step(const std::string& verdict,
                   folioedit::ResolutionState rs,
                   int laps = 0);

}  // namespace Folio
