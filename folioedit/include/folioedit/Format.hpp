#pragma once
//
// folioedit :: Format -- the plaintext that gets sealed: the three top-level
// parts (body + annotations + custody), and their JSON round-trip.
//
//   body        -- selected scenes' text (HTML for offset fidelity) + the
//                  AI-only `pass` briefing (rules the human CLI never renders).
//   annotations -- the appended return block; ONE shape whether AI or CLI wrote
//                  it; each entry carries BOTH range and quote so import can
//                  re-anchor offset -> quote -> floating (s7).
//   custody     -- the hash-chained trail (Custody.hpp).
//
// This is the engine's OWN annotation shape -- deliberately not Folio's
// DocumentModel::Annotation. The engine never links DocumentModel; Folio's
// import glue maps between the two. (DESIGN_editorialization s4 / s16.7.)
//
// nlohmann-only + STL. Pure, sandbox-testable end to end.
//
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "folioedit/Custody.hpp"

namespace folioedit {

using json = nlohmann::json;

// -- body ---------------------------------------------------------------------
struct Scene {
    std::string iid;      // stable scene id (anchors are local to this)
    std::string title;
    int         order = 0;
    std::string text;     // HTML (offsets match Folio's in-app buffer)
};

struct Pass {
    std::string              id;      // this pass's id
    std::string              source;  // editor identity stamped on every annotation
    std::vector<std::string> kinds;   // hats this pass may use
    std::string              rules;   // AI-only briefing (surface-not-verdict law)
};

// -- annotations (the return block) -------------------------------------------
struct Annotation {
    std::string scene_iid;
    int         range_start = 0;   // char offsets WITHIN that scene
    int         range_end   = 0;
    std::string quote;             // exact spanned text -- re-anchor fallback
    std::string kind;              // hat: Proofreader / Editor / Writer
    std::string text;              // the comment
    bool        withdrawn = false; // a `del`'d annotation is a TOMBSTONE, not a
                                   // deletion: it stays in the block (so the seal
                                   // commits that it existed and was withdrawn --
                                   // a court-visible trace), just marked withdrawn.
};

// -- the whole sealed document ------------------------------------------------
struct Document {
    std::string project_id;
    std::string project_title;
    std::string version_stamp;              // drift detection on import

    Pass                      pass;
    std::vector<Scene>        scenes;        // body
    std::vector<Annotation>   annotations;   // appended return block
    std::vector<CustodyEvent> custody;       // hash-chained trail
};

// JSON round-trip (defined in Format.cpp). Byte-stable field order so a resealed
// file diffs cleanly.
json     to_json(const Document& doc);
Document from_json(const json& j);

}  // namespace folioedit
