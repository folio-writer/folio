#pragma once
//
// folioedit :: Custody -- the append-only, tamper-evident chain of custody.
//
// One CustodyEvent per hand-off. The chain proves ORDER + COMPLETENESS:
//   hash = SHA256(prev_hash + event-contents)
// remove / reorder / edit-a-field and the recompute breaks. The AES seal (over
// the whole plaintext, custody included) proves the file was untouched after
// sealing. Two layers: chain = internal order/integrity; seal = whole-file.
//
// "Who" is carried by actor_id (a TOFU key fingerprint) + an Ed25519 signature
// over the event hash; "when" by an optional RFC-3161 timestamp_token, degrading
// to the local clock when offline (the file records which it got).
// (DESIGN_editorialization s16.3 / s16.4.)
//
// SHA-256 / Ed25519 come from libcrypto -- so Custody.cpp links OpenSSL, but this
// HEADER stays crypto-type-free (the rule is: no EVP/openssl types in public
// headers; only the .cpp sees <openssl/*>).
//
#include <string>
#include <vector>

namespace folioedit {

enum class CustodyEvent_Kind {
    Issued,    // export stamped the sent scene versions (binds body hash)
    Sealed,    // a pass sealed its returned annotations block (binds ann. hash)
    Imported,  // Folio imported + verified the returned file
};

enum class TimeSource {
    LocalClock,   // offline fallback -- disputable "when"
    Rfc3161,      // trusted timestamp token present
};

struct CustodyEvent {
    int               seq   = 0;
    CustodyEvent_Kind kind  = CustodyEvent_Kind::Issued;
    std::string       actor;               // human label ("jane")
    std::string       actor_id;            // TOFU key fingerprint
    std::string       at;                  // ISO-8601 timestamp
    std::string       binds;               // content hash this event commits to
    std::string       prev_hash;           // previous event's hash ("" for seq 0)
    std::string       hash;                // SHA256(prev_hash + contents)
    std::string       signature;           // Ed25519 over hash (hex)
    TimeSource        time_source = TimeSource::LocalClock;
    std::string       timestamp_token;     // RFC-3161 token (base64), when present
};

// enum <-> string (single source of truth; Format reuses these for JSON).
std::string kind_to_str(CustodyEvent_Kind k);
CustodyEvent_Kind kind_from_str(const std::string& s);
std::string time_to_str(TimeSource t);
TimeSource time_from_str(const std::string& s);

// SHA-256 as lowercase hex (via libcrypto; defined in Custody.cpp).
std::string sha256_hex(const std::string& data);

// hash = SHA256(prev_hash + contents).
std::string chain_hash(const std::string& prev_hash, const std::string& contents);

// The exact bytes an event's hash is computed over: an explicit, length-prefixed
// concatenation of the BOUND fields, in fixed order. EXCLUDES hash + signature
// (derived from it) AND time_source + timestamp_token: an RFC-3161 token certifies
// this hash, so it cannot be an input to it -- the token and its source label are
// post-finalize evidence, verified against the hash, never bound inside it.
// Deliberately NOT a JSON dump -- a forensic hash must not be hostage to any
// library's formatting across versions.
std::string canonical_contents(const CustodyEvent& e);

// event_hash = chain_hash(e.prev_hash, canonical_contents(e)).
std::string event_hash(const CustodyEvent& e);

// Link an event onto the chain: set prev_hash, recompute + store hash. Does NOT
// sign (Ed25519 signing needs the keypair; separate step). Returns the new hash.
std::string finalize_event(CustodyEvent& e, const std::string& prev_hash);

// Append an event to a chain: assigns seq = chain.size() and prev_hash = the last
// event's hash (or "" if first), then finalizes. The caller fills kind / actor /
// actor_id / at / binds BEFORE calling -- actor_id must be set pre-finalize so the
// hash binds the identity. Returns a reference to the appended, finalized event so
// it can then be signed (sign_event) and/or timestamped (try_timestamp_event).
CustodyEvent& append_event(std::vector<CustodyEvent>& chain, CustodyEvent e);

// Recompute the chain and verify every link: sequential seq, prev_hash linkage,
// and each stored hash == event_hash. Integrity + order only -- signature and
// timestamp verification are separate passes (they need keys / a TSA cert).
bool verify_chain(const std::vector<CustodyEvent>& chain);

}  // namespace folioedit
