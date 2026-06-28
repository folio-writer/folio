// ─────────────────────────────────────────────────────────────────────────────
// Iid.cpp — stable cross-layer part identity (s19). Pure; see Iid.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "Iid.hpp"

#include <random>

namespace Folio {

namespace {

// Crockford base32 minus the ambiguous set (i, l, o, u) — case-insensitive,
// filename-safe, unambiguous in logs / read aloud.
constexpr char B32[] = "0123456789abcdefghjkmnpqrstvwxyz";  // 32 chars
constexpr int  B32_N = 32;

// Thread-local PRNG seeded once. random_device for the seed; the suffix only
// needs to be collision-resistant within a project (and across handoff copies),
// not cryptographic.
std::mt19937_64& rng() {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    return gen;
}

bool is_b32_char(char c) {
    for (int i = 0; i < B32_N; ++i) if (B32[i] == c) return true;
    return false;
}

}  // namespace

const char* iid_prefix(IidKind k) {
    switch (k) {
        case IidKind::Scene:     return "scn";
        case IidKind::Group:     return "grp";
        case IidKind::Character: return "chr";
        case IidKind::Place:     return "plc";
        case IidKind::Reference: return "ref";
        case IidKind::Template:  return "tpl";
        case IidKind::Asset:     return "ast";
        case IidKind::Snapshot:  return "snp";
        case IidKind::KeyPoint:  return "kp";
        case IidKind::Thread:    return "thr";
        case IidKind::Unknown:   return "unk";
    }
    return "unk";
}

IidKind iid_kind_of(const std::string& iid) {
    auto us = iid.find('_');
    std::string p = (us == std::string::npos) ? iid : iid.substr(0, us);
    if (p == "scn") return IidKind::Scene;
    if (p == "grp") return IidKind::Group;
    if (p == "chr") return IidKind::Character;
    if (p == "plc") return IidKind::Place;
    if (p == "ref") return IidKind::Reference;
    if (p == "tpl") return IidKind::Template;
    if (p == "ast") return IidKind::Asset;
    if (p == "snp") return IidKind::Snapshot;
    if (p == "kp")  return IidKind::KeyPoint;
    if (p == "thr") return IidKind::Thread;
    return IidKind::Unknown;
}

std::string make_iid(IidKind kind, int entropy_chars) {
    if (entropy_chars < 4)  entropy_chars = 4;
    if (entropy_chars > 32) entropy_chars = 32;
    std::string s = iid_prefix(kind);
    s += '_';
    std::uniform_int_distribution<int> d(0, B32_N - 1);
    for (int i = 0; i < entropy_chars; ++i) s += B32[d(rng())];
    return s;
}

bool is_iid(const std::string& s) {
    auto us = s.find('_');
    if (us == std::string::npos || us == 0 || us + 1 >= s.size()) return false;
    if (iid_kind_of(s) == IidKind::Unknown) return false;  // unknown prefix
    for (std::size_t i = us + 1; i < s.size(); ++i)
        if (!is_b32_char(s[i])) return false;
    return true;
}

std::string iid_suffix(const std::string& iid) {
    auto us = iid.find('_');
    if (us == std::string::npos || us + 1 >= iid.size()) return "";
    return iid.substr(us + 1);
}

std::string widget_name(const std::string& role, const std::string& iid) {
    if (iid.empty()) return role;
    return role + "-" + iid;
}

std::string iid_from_widget_name(const std::string& name) {
    // Scan for a "<known-prefix>_<b32...>" token anywhere after a '-' or start.
    // Widget names are "<role>-<iid>"; the role itself may contain '-', so we
    // look for the last hyphen-delimited token that parses as an iid.
    std::size_t pos = 0;
    std::string best;
    while (pos < name.size()) {
        std::size_t dash = name.find('-', pos);
        std::string tok = (dash == std::string::npos)
                              ? name.substr(pos)
                              : name.substr(pos, dash - pos);
        if (is_iid(tok)) best = tok;  // last valid wins (the trailing iid)
        if (dash == std::string::npos) break;
        pos = dash + 1;
    }
    return best;
}

}  // namespace Folio
