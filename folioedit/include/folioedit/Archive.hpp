#pragma once
//
// folioedit :: Archive -- open/save a whole .folioedit file: the top-level
// orchestration a CLI (or Folio) calls. Ties Format (Document <-> JSON), the
// Envelope frame, and the Seal into read/write helpers, with raw-key and
// passphrase variants, plus the content hashes a custody event's `binds` commits
// to. Pure STL here (fstream); the crypto is delegated to Seal. (s16.7.)
//
// Dispatch tip for a CLI: read_envelope_file(path).kdf_id tells you whether the
// file wants a raw key (KdfId::None -> -k) or a passphrase (Pbkdf2 -> -p).
//
#include <string>

#include "folioedit/Envelope.hpp"
#include "folioedit/Format.hpp"

namespace folioedit {

// ── raw envelope frame on disk ───────────────────────────────────────────────
void     write_envelope_file(const std::string& path, const Envelope& env);
Envelope read_envelope_file(const std::string& path);

// ── whole-document open/save, raw 32-byte key ────────────────────────────────
// save: doc -> JSON -> seal(key) -> envelope -> file (kdf_id None).
// open: file -> envelope -> unseal(key) -> parse -> Document.
// A wrong key / tampered file throws (GCM tag failure -- no partial open).
void     save_document(const std::string& path, const Document& doc, const bytes& key);
Document open_document(const std::string& path, const bytes& key);

// ── whole-document open/save, passphrase (PBKDF2) ────────────────────────────
// save: fresh salt, derive, seal; the envelope records kdf_id + iters + salt.
// open: re-derive from the recorded salt/iters, unseal. Wrong passphrase throws.
void     save_document_pw(const std::string& path, const Document& doc,
                          const std::string& passphrase);
Document open_document_pw(const std::string& path, const std::string& passphrase);

// ── content hashes for custody `binds` ───────────────────────────────────────
// Stable, explicit, length-prefixed (like canonical_contents -- never a JSON
// dump). `issued` binds body_hash (the scene versions sent); `sealed` binds
// annotations_hash (the returned block). Recomputable on import to detect drift.
std::string body_hash(const Document& doc);
std::string annotations_hash(const Document& doc);

}  // namespace folioedit
