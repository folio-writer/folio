#pragma once
//
// folioedit :: Timestamp -- the "when": RFC-3161 trusted timestamps over a
// custody event's hash.
//
// A local clock is disputable ("you back-dated it"). A trusted timestamp is a
// TSA's signed assertion that a given hash existed at a given time -- the one
// piece of "when" that holds up against a hostile challenge. It is OPTIONAL and
// per-event: reach the TSA when online, degrade to the local clock when not, and
// the event records which it got (time_source). (DESIGN_editorialization s16.4.)
//
// The engine stays OFFLINE-PURE. The one online action -- POSTing the request to
// a TSA over HTTP -- is an INJECTED transport function, supplied by Folio / the
// CLI, so the engine never links libcurl and never opens a socket. Everything
// here (build the request, verify the token, extract the time) is libcrypto only.
//
// RFC-3161 / ts.h via libcrypto, entirely in Timestamp_openssl.cpp -- no OpenSSL
// types leak into this header (matching Seal.hpp / Identity.hpp).
//
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "folioedit/Custody.hpp"

namespace folioedit {

using ts_bytes = std::vector<std::uint8_t>;

// The DER-encoded RFC-3161 request (a TimeStampReq) to POST to a TSA, plus the
// random nonce bound into it (echoed in the token; guards against replay).
struct TimestampRequest {
    ts_bytes der;     // the application/timestamp-query body
    ts_bytes nonce;   // random nonce inside the request
};

// Build a TSQ over an event hash-hex: a SHA-256 message imprint of the hash's raw
// bytes, a fresh nonce, cert_req set (ask the TSA to include its cert). Throws on
// a malformed hash or a crypto failure.
TimestampRequest make_timestamp_request(const std::string& hash_hex);

// The one online seam, INJECTED: given the DER TSQ, return the DER TSR. Folio /
// the CLI implement it as an HTTP POST to the TSA. Throw (or return empty) to
// signal "offline / TSA unreachable" -- stamping then degrades to the local clock.
using TimestampTransport = std::function<ts_bytes(const ts_bytes& tsq_der)>;

// A verified timestamp: the TSA-asserted UTC time and the token to store.
struct TimestampProof {
    std::string time_iso;    // e.g. "2026-07-01T12:00:00Z"
    std::string token_b64;   // the DER TimeStampToken, base64 (-> timestamp_token)
};

// Request -> fetch (via transport) -> verify a timestamp over hash_hex.
// trust_pem is the PEM of the TSA signer/CA cert(s) to verify against. Throws if
// the transport fails or the token does not verify (bad signature, wrong imprint,
// wrong nonce, or an untrusted signer).
TimestampProof fetch_timestamp(const std::string& hash_hex,
                               const TimestampTransport& transport,
                               const std::string& trust_pem);

// Re-verify a STORED token (on import) against the event hash + trust anchor.
// No nonce is checked (the original request is gone); signature + imprint + trust
// are. Returns the trusted time (ISO-8601); throws on any verification failure.
std::string verify_timestamp_token(const std::string& token_b64,
                                   const std::string& hash_hex,
                                   const std::string& trust_pem);

// -- CustodyEvent convenience -------------------------------------------------
// Try to trusted-timestamp a FINALIZED event. On success: time_source = Rfc3161
// and timestamp_token = the token. On ANY failure (offline, TSA down, verify
// fail): leaves time_source = LocalClock and the token empty -- degrade, never
// block. Returns true iff a trusted token was attached.
//
// It NEVER touches a bound field: e.at (the actor's local claim) and e.hash stay
// as finalized. time_source + timestamp_token are excluded from the hash, so
// attaching a token needs no re-finalize and no re-sign -- the token's own
// TSA-asserted time is read back via verify_timestamp_token when the trusted
// "when" is needed.
bool try_timestamp_event(CustodyEvent& e, const TimestampTransport& transport,
                         const std::string& trust_pem);

}  // namespace folioedit
