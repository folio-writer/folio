#pragma once
//
// folioedit :: Archive -- open/save a whole .folioedit file: the top-level
// orchestration a CLI (or Folio) calls. Ties Format (Document <-> JSON), the
// Envelope frame, and the Seal into read/write helpers, with raw-key and
// passphrase variants, plus the content hashes a custody event's `binds` commits
// to. Pure STL here (fstream); the crypto is delegated to Seal. (s16.7.)
//
// Dispatch tip for a CLI: read_envelope_file(path).kdf_id tells you whether the
// file wants a raw key (KdfId::None -> -k) or a passphrase (Pbkdf2 -> -p). Or
// content-sniff without a key via peek_file_face() below.
//
// Two faces (s18): the SEALED read/save (AES/PBKDF2) are always DECLARED here,
// but only DEFINED in the sealed build -- a plain build (FOLIOEDIT_NO_CRYPTO)
// omits their definitions (Archive.cpp), so a plain consumer that never calls
// them links clean, while the GUI app (always sealed) still sees the full API.
// The PLAIN read/save + content sniff + the `binds` hashes are always present
// and pure, so folioedit-plain still opens/annotates/saves an unencrypted file.
//
#include <string>

#include "folioedit/Envelope.hpp"
#include "folioedit/Format.hpp"

namespace folioedit {

// ── raw envelope frame on disk (pure framing; no crypto) ─────────────────────
void     write_envelope_file(const std::string& path, const Envelope& env);
Envelope read_envelope_file(const std::string& path);

// ── whole-document open/save, raw 32-byte key (SEALED face) ──────────────────
// save: doc -> JSON -> seal(key) -> envelope -> file (kdf_id None).
// open: file -> envelope -> unseal(key) -> parse -> Document.
// A wrong key / tampered file throws (GCM tag failure -- no partial open).
// DEFINED only in the sealed build (needs libcrypto behind Seal).
void     save_document(const std::string& path, const Document& doc, const bytes& key);
Document open_document(const std::string& path, const bytes& key);

// ── whole-document open/save, passphrase (PBKDF2) (SEALED face) ──────────────
// save: fresh salt, derive, seal; the envelope records kdf_id + iters + salt.
// open: re-derive from the recorded salt/iters, unseal. Wrong passphrase throws.
// DEFINED only in the sealed build.
void     save_document_pw(const std::string& path, const Document& doc,
                          const std::string& passphrase);
Document open_document_pw(const std::string& path, const std::string& passphrase);

// ── plain (unsealed) whole-document open/save (PLAIN face, s18.2/18.3) ───────
// The readable Document JSON itself -- no envelope, no crypto -- with a leading
// `instructions` block telling an AI to APPEND to `annotations` (never touch
// scenes) and return the file. Pure STL: present in BOTH faces, links no
// libcrypto, so a -DFOLIOEDIT_NO_CRYPTO build opens/annotates/saves it. The file
// still carries an unsigned `issued` custody event binding body_hash, so Absorb
// can prove only-appended (s18.5). Unknown JSON keys (incl. `instructions`) are
// ignored on read, so the round-trip is loss-free for the engine's own fields.
void     save_document_plain(const std::string& path, const Document& doc);
Document open_document_plain(const std::string& path);

// ── content-sniff a file's face by its leading bytes (pure, no key) ──────────
// A sealed file starts with the MAGIC ("FOLIOEDIT"); a plain file is JSON, so it
// starts with '{' (after optional whitespace). Absorb / the CLI dispatch on this
// without needing a key. Unknown = neither (empty / unreadable / garbage).
enum class FileFace { Plain, Sealed, Unknown };
FileFace peek_file_face(const std::string& path);

// ── content hashes for custody `binds` ───────────────────────────────────────
// Stable, explicit, length-prefixed (like canonical_contents -- never a JSON
// dump). `issued` binds body_hash (the scene versions sent); `sealed` binds
// annotations_hash (the returned block). Recomputable on import to detect drift.
std::string body_hash(const Document& doc);
std::string annotations_hash(const Document& doc);

}  // namespace folioedit
