//
// folioedit :: Custody -- the hash-chained, tamper-evident trail.
//
// Self-contained: the enums and their string forms, the canonical bytes that get
// hashed, SHA-256 (via libcrypto EVP), the chain linkage, and verification all
// live here. No nlohmann -- the forensic hash is defined by our own explicit
// canonical form, not any library's serialization. (DESIGN_editorialization
// s16.3 / s16.4.)
//
// SHA-256 uses EVP_Digest (the non-deprecated one-shot) rather than the legacy
// SHA256() call, which OpenSSL 3 deprecates -- important under -Werror.
//
#include "folioedit/Custody.hpp"

#include <stdexcept>

#include <openssl/evp.h>

namespace folioedit {

// ── enum <-> string ──────────────────────────────────────────────────────────
std::string kind_to_str(CustodyEvent_Kind k) {
    switch (k) {
        case CustodyEvent_Kind::Issued:   return "issued";
        case CustodyEvent_Kind::Sealed:   return "sealed";
        case CustodyEvent_Kind::Imported: return "imported";
    }
    throw std::runtime_error("folioedit: unknown CustodyEvent_Kind");
}
CustodyEvent_Kind kind_from_str(const std::string& s) {
    if (s == "issued")   return CustodyEvent_Kind::Issued;
    if (s == "sealed")   return CustodyEvent_Kind::Sealed;
    if (s == "imported") return CustodyEvent_Kind::Imported;
    throw std::runtime_error("folioedit: unknown custody event '" + s + "'");
}
std::string time_to_str(TimeSource t) {
    switch (t) {
        case TimeSource::LocalClock: return "local";
        case TimeSource::Rfc3161:    return "rfc3161";
    }
    throw std::runtime_error("folioedit: unknown TimeSource");
}
TimeSource time_from_str(const std::string& s) {
    if (s == "local")   return TimeSource::LocalClock;
    if (s == "rfc3161") return TimeSource::Rfc3161;
    throw std::runtime_error("folioedit: unknown time_source '" + s + "'");
}

// ── SHA-256 ──────────────────────────────────────────────────────────────────
std::string sha256_hex(const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int  md_len = 0;
    if (EVP_Digest(data.data(), data.size(), md, &md_len, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("folioedit: SHA-256 failed");

    static const char* hexd = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(md_len) * 2);
    for (unsigned int i = 0; i < md_len; ++i) {
        out.push_back(hexd[(md[i] >> 4) & 0x0F]);
        out.push_back(hexd[md[i] & 0x0F]);
    }
    return out;
}

std::string chain_hash(const std::string& prev_hash, const std::string& contents) {
    return sha256_hex(prev_hash + contents);
}

// ── canonical contents ───────────────────────────────────────────────────────
// Explicit, length-prefixed, fixed order. Every variable field is written as
// key=LEN:value so no concatenation is ambiguous (e.g. actor "ab" + id "c" can
// never collide with actor "a" + id "bc"). hash + signature are excluded --
// they are derived from this, not bound by it.
std::string canonical_contents(const CustodyEvent& e) {
    std::string c = "folioedit.custody.v1";
    auto add_str = [&](const char* key, const std::string& val) {
        c += '\n';
        c += key;
        c += '=';
        c += std::to_string(val.size());
        c += ':';
        c += val;
    };
    auto add_int = [&](const char* key, long long val) {
        c += '\n';
        c += key;
        c += '=';
        c += std::to_string(val);
    };
    add_int("seq",             e.seq);
    add_str("event",           kind_to_str(e.kind));
    add_str("actor",           e.actor);
    add_str("actor_id",        e.actor_id);
    add_str("at",              e.at);
    add_str("binds",           e.binds);
    add_str("prev_hash",       e.prev_hash);
    // NB: time_source + timestamp_token are DELIBERATELY excluded. An RFC-3161
    // token certifies this event's hash, so it cannot be an input to that hash
    // (circular); it -- and the local/trusted source label -- are evidence
    // gathered AFTER finalize, verified against the hash, never bound into it.
    return c;
}

std::string event_hash(const CustodyEvent& e) {
    return chain_hash(e.prev_hash, canonical_contents(e));
}

std::string finalize_event(CustodyEvent& e, const std::string& prev_hash) {
    e.prev_hash = prev_hash;
    e.hash      = event_hash(e);
    return e.hash;
}

CustodyEvent& append_event(std::vector<CustodyEvent>& chain, CustodyEvent e) {
    e.seq = static_cast<int>(chain.size());
    const std::string prev = chain.empty() ? std::string{} : chain.back().hash;
    finalize_event(e, prev);
    chain.push_back(std::move(e));
    return chain.back();
}

// ── verify ───────────────────────────────────────────────────────────────────
bool verify_chain(const std::vector<CustodyEvent>& chain) {
    std::string prev;   // "" before the first event
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const CustodyEvent& e = chain[i];
        if (e.seq != static_cast<int>(i))  return false;   // sequential, gap-free
        if (e.prev_hash != prev)           return false;   // linkage
        if (e.hash != event_hash(e))       return false;   // integrity
        prev = e.hash;
    }
    return true;
}

}  // namespace folioedit
