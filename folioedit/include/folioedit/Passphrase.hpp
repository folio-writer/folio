#pragma once
//
// folioedit :: Passphrase -- the human face of the seal key.
//
// A .folioedit file is sealed with a key derived (PBKDF2) from a passphrase. A
// passphrase the author has to INVENT is friction; a random one they can't read
// back is lost the moment they close the window. So Folio GENERATES one, and
// makes it a little absurd image -- four words, subject/verb/adjective/object --
// so it's memorable, transcribable, and (Scott's word) amusing:
//
//     "otters unionize discount turnips"
//
// The whimsy isn't decoration: an author who enjoys reading the phrase is one who
// copies it correctly and doesn't lose it. The phrase is what lands in the
// interchange ledger and what the editor is sent; the AES key derives from it and
// is never shown.
//
// TWO functions, one law:
//   generate_passphrase()    -- make a fresh absurd four-word phrase (display form,
//                               space-separated, all lowercase ASCII).
//   canonicalize_passphrase() -- the SEAM. Fold a typed phrase to the exact bytes
//                               the KDF sees, so spacing/case/quoting can't change
//                               the derived key. Run identically on seal and open
//                               (it lives inside seal_with_passphrase /
//                               unseal_with_passphrase) so a phrase generated here,
//                               typed by a human, or pasted with hyphens all derive
//                               ONE key. This is what killed the "forgot to quote"
//                               failure: spaces never reach the key.
//
// Pure STL -- no crypto, no json, no gtk. Sandbox-testable end to end, like KeyHex
// and Vocabulary. (DESIGN_editorialization s16.1 / s16.5; s102 interchange.)
//
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace folioedit {

// ── the seam: phrase -> canonical bytes the KDF derives from ─────────────────
// Lowercases ASCII A-Z and removes every space, tab, newline, hyphen, and
// underscore. The whimsy's entropy is in WHICH words, never the spacing -- so
// folding spacing/case away loses nothing and makes the key robust to how the
// phrase was typed. Non-ASCII bytes pass through untouched (pools are ASCII).
// Deterministic and idempotent: canonicalize(canonicalize(x)) == canonicalize(x).
std::string canonicalize_passphrase(const std::string& phrase);

// ── the generator ────────────────────────────────────────────────────────────
// The four word pools (subject verb adjective object), exposed so a test can
// assert their sizes / the entropy floor, and so a face could preview them.
enum class Slot { Subject = 0, Verb, Adjective, Object };
const std::vector<std::string>& pool(Slot s);

// Pluggable randomness: pick(n) must return a uniformly-random index in [0, n).
// Injected so tests are deterministic; the default overload below uses the
// platform CSPRNG (std::random_device, /dev/urandom on Fedora).
using IndexRng = std::function<std::size_t(std::size_t)>;

// Build a phrase by drawing one word from each slot, joined by single spaces --
// the DISPLAY form (what the ledger stores and the editor is sent). Feed it to
// seal_with_passphrase as-is; the seal canonicalizes at the door.
std::string generate_passphrase(const IndexRng& pick);
std::string generate_passphrase();   // default: platform CSPRNG

// Entropy of one generated phrase in bits, given current pool sizes:
// log2(|subject| * |verb| * |adjective| * |object|). Reported so the tradeoff
// (memorability caps entropy; buy it back with pool SIZE, not more words) stays
// visible -- and so we know how far the pools must grow. The real wall against an
// offline guess is the KDF's 600k iterations; this is the space in front of it.
double phrase_entropy_bits();

}  // namespace folioedit
