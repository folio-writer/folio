// ─────────────────────────────────────────────────────────────────────────────
// NoteStep.cpp — s108 §26. Pure; see NoteStep.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "NoteStep.hpp"

namespace Folio {

namespace {
std::string loop_badge(int laps) {
    // The lap count falls out of the send-back links on the custody chain (§25.5);
    // show it only once the note has gone around at least once (round >= 2).
    if (laps > 1) return "In the loop \u00b7 round " + std::to_string(laps);
    return "In the loop";
}
}  // namespace

NoteStep note_step(const std::string& verdict,
                   folioedit::ResolutionState rs,
                   int laps) {
    using RS = folioedit::ResolutionState;
    NoteStep s;

    // Resolution first: once a key has turned, the note is resolving or done.
    switch (rs) {
        case RS::Resolved:
            s.phase = NoteStep::Phase::Done;
            s.badge = "Done";
            s.next_action.clear();          // off the list (§26.3)
            return s;
        case RS::HalfEditor:
            s.phase = NoteStep::Phase::Resolving;
            s.badge = "Resolving";
            s.next_action = "Editor resolved \u2014 confirm to finish";
            return s;
        case RS::HalfAuthor:
            s.phase = NoteStep::Phase::Resolving;
            s.badge = "Resolving";
            s.next_action = "Waiting on the editor to agree";
            return s;
        case RS::Open:
            break;                          // fall through to verdict
    }

    // Resolution open -> the verdict places the note.
    if (verdict == "accepted") {
        s.phase = NoteStep::Phase::InLoop;
        s.badge = loop_badge(laps);
        s.next_action = "Acknowledged \u2014 correct the text, then send back";
        return s;
    }
    if (verdict == "declined") {
        s.phase = NoteStep::Phase::InLoop;
        s.badge = loop_badge(laps);
        s.next_action = "Declined \u2014 send back so the editor sees your reasoning";
        return s;
    }

    // "" / "proposed" / anything else -> the way in.
    s.phase = NoteStep::Phase::Proposed;
    s.badge = "Proposed";
    s.next_action = "Acknowledge or decline";
    return s;
}

}  // namespace Folio
