//
// folioedit :: Format -- the plaintext JSON round-trip + the binary envelope
// framing. Both PURE (no OpenSSL): the Document (body + annotations + custody)
// serialises to/from JSON; the Envelope (the on-disk wrapper) serialises to/from
// a length-delimited byte frame. Sandbox-testable end to end.
//
// House style: plain nlohmann::json (deterministic alphabetical dump), read back
// with j.value(key, default). All fields always emitted -- a resealed file diffs
// cleanly and round-trips byte-identical.
//
#include "folioedit/Format.hpp"
#include "folioedit/Envelope.hpp"

#include <cstring>
#include <stdexcept>

namespace folioedit {
namespace {

// (enum <-> string live in Custody -- single source of truth; used below.)

// ── per-record JSON ──────────────────────────────────────────────────────────
json scene_to_json(const Scene& s) {
    json j;
    j["iid"]   = s.iid;
    j["order"] = s.order;
    j["text"]  = s.text;
    j["title"] = s.title;
    return j;
}
Scene scene_from_json(const json& j) {
    Scene s;
    s.iid   = j.value("iid",   std::string{});
    s.title = j.value("title", std::string{});
    s.order = j.value("order", 0);
    s.text  = j.value("text",  std::string{});
    return s;
}

json pass_to_json(const Pass& p) {
    json j;
    j["id"]     = p.id;
    j["kinds"]  = p.kinds;
    j["rules"]  = p.rules;
    j["source"] = p.source;
    return j;
}
Pass pass_from_json(const json& j) {
    Pass p;
    p.id     = j.value("id",     std::string{});
    p.source = j.value("source", std::string{});
    p.rules  = j.value("rules",  std::string{});
    if (j.contains("kinds") && j.at("kinds").is_array()) {
        for (const auto& k : j.at("kinds")) p.kinds.push_back(k.get<std::string>());
    }
    return p;
}

json annotation_to_json(const Annotation& a) {
    json j;
    j["kind"]        = a.kind;
    j["quote"]       = a.quote;
    j["range_end"]   = a.range_end;
    j["range_start"] = a.range_start;
    j["scene_iid"]   = a.scene_iid;
    j["text"]        = a.text;
    j["withdrawn"]   = a.withdrawn;
    return j;
}
Annotation annotation_from_json(const json& j) {
    Annotation a;
    a.scene_iid   = j.value("scene_iid",   std::string{});
    a.range_start = j.value("range_start", 0);
    a.range_end   = j.value("range_end",   0);
    a.quote       = j.value("quote",       std::string{});
    a.kind        = j.value("kind",        std::string{});
    a.text        = j.value("text",        std::string{});
    a.withdrawn   = j.value("withdrawn",   false);
    return a;
}

json custody_to_json(const CustodyEvent& e) {
    json j;
    j["actor"]           = e.actor;
    j["actor_id"]        = e.actor_id;
    j["at"]              = e.at;
    j["binds"]           = e.binds;
    j["event"]           = kind_to_str(e.kind);
    j["hash"]            = e.hash;
    j["prev_hash"]       = e.prev_hash;
    j["seq"]             = e.seq;
    j["signature"]       = e.signature;
    j["time_source"]     = time_to_str(e.time_source);
    j["timestamp_token"] = e.timestamp_token;
    return j;
}
CustodyEvent custody_from_json(const json& j) {
    CustodyEvent e;
    e.seq             = j.value("seq", 0);
    e.kind            = kind_from_str(j.value("event", std::string{"issued"}));
    e.actor           = j.value("actor",           std::string{});
    e.actor_id        = j.value("actor_id",        std::string{});
    e.at              = j.value("at",              std::string{});
    e.binds           = j.value("binds",           std::string{});
    e.prev_hash       = j.value("prev_hash",       std::string{});
    e.hash            = j.value("hash",            std::string{});
    e.signature       = j.value("signature",       std::string{});
    e.time_source     = time_from_str(j.value("time_source", std::string{"local"}));
    e.timestamp_token = j.value("timestamp_token", std::string{});
    return e;
}

// ── little-endian frame primitives (pure byte framing) ───────────────────────
void put_u32(bytes& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void put_u64(bytes& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void put_blob32(bytes& out, const bytes& b) {
    if (b.size() > 0xFFFFFFFFull) throw std::runtime_error("folioedit: blob too large for u32 frame");
    put_u32(out, static_cast<std::uint32_t>(b.size()));
    out.insert(out.end(), b.begin(), b.end());
}
void put_blob64(bytes& out, const bytes& b) {
    put_u64(out, static_cast<std::uint64_t>(b.size()));
    out.insert(out.end(), b.begin(), b.end());
}

std::uint32_t get_u32(const bytes& in, std::size_t& off) {
    if (off + 4 > in.size()) throw std::runtime_error("folioedit: truncated envelope (u32)");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(in[off + static_cast<std::size_t>(i)]) << (8 * i);
    off += 4;
    return v;
}
std::uint64_t get_u64(const bytes& in, std::size_t& off) {
    if (off + 8 > in.size()) throw std::runtime_error("folioedit: truncated envelope (u64)");
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(in[off + static_cast<std::size_t>(i)]) << (8 * i);
    off += 8;
    return v;
}
bytes get_blob(const bytes& in, std::size_t& off, std::uint64_t len) {
    if (off + len > in.size()) throw std::runtime_error("folioedit: truncated envelope (blob)");
    bytes b(in.begin() + static_cast<std::ptrdiff_t>(off),
            in.begin() + static_cast<std::ptrdiff_t>(off + len));
    off += len;
    return b;
}

}  // namespace

// ── Document <-> JSON ────────────────────────────────────────────────────────
json to_json(const Document& doc) {
    json j;

    json anns = json::array();
    for (const auto& a : doc.annotations) anns.push_back(annotation_to_json(a));
    j["annotations"] = std::move(anns);

    json cust = json::array();
    for (const auto& e : doc.custody) cust.push_back(custody_to_json(e));
    j["custody"] = std::move(cust);

    j["pass"] = pass_to_json(doc.pass);

    json proj;
    proj["id"]            = doc.project_id;
    proj["title"]         = doc.project_title;
    proj["version_stamp"] = doc.version_stamp;
    j["project"] = std::move(proj);

    json scenes = json::array();
    for (const auto& s : doc.scenes) scenes.push_back(scene_to_json(s));
    j["scenes"] = std::move(scenes);

    return j;
}

Document from_json(const json& j) {
    Document doc;

    if (j.contains("project")) {
        const json& p = j.at("project");
        doc.project_id    = p.value("id",            std::string{});
        doc.project_title = p.value("title",         std::string{});
        doc.version_stamp = p.value("version_stamp", std::string{});
    }
    if (j.contains("pass")) doc.pass = pass_from_json(j.at("pass"));

    if (j.contains("scenes") && j.at("scenes").is_array())
        for (const auto& s : j.at("scenes")) doc.scenes.push_back(scene_from_json(s));

    if (j.contains("annotations") && j.at("annotations").is_array())
        for (const auto& a : j.at("annotations")) doc.annotations.push_back(annotation_from_json(a));

    if (j.contains("custody") && j.at("custody").is_array())
        for (const auto& e : j.at("custody")) doc.custody.push_back(custody_from_json(e));

    return doc;
}

// ── Envelope <-> bytes (length-delimited frame) ──────────────────────────────
// Layout: MAGIC(9) | schema(u32) | cipher(u8) | kdf_id(u8) | kdf_iters(u32)
//         | salt(blob32) | nonce(blob32) | ciphertext(blob64) | tag(blob32)
bytes envelope_to_bytes(const Envelope& env) {
    bytes out;
    const char* m = MAGIC;
    out.insert(out.end(), m, m + std::strlen(m));   // 9 bytes, no null
    put_u32(out, static_cast<std::uint32_t>(env.schema));
    out.push_back(static_cast<std::uint8_t>(env.cipher));
    out.push_back(static_cast<std::uint8_t>(env.kdf_id));
    put_u32(out, env.kdf_iters);
    put_blob32(out, env.salt);
    put_blob32(out, env.nonce);
    put_blob64(out, env.ciphertext);
    put_blob32(out, env.tag);
    return out;
}

Envelope envelope_from_bytes(const bytes& raw) {
    const std::size_t mlen = std::strlen(MAGIC);
    if (raw.size() < mlen || std::memcmp(raw.data(), MAGIC, mlen) != 0)
        throw std::runtime_error("folioedit: not a .folioedit file (bad magic)");

    std::size_t off = mlen;
    Envelope env;
    env.schema = static_cast<int>(get_u32(raw, off));
    if (off >= raw.size()) throw std::runtime_error("folioedit: truncated envelope (cipher)");
    env.cipher = static_cast<CipherId>(raw[off]);
    ++off;
    if (off >= raw.size()) throw std::runtime_error("folioedit: truncated envelope (kdf_id)");
    env.kdf_id = static_cast<KdfId>(raw[off]);
    ++off;
    env.kdf_iters  = get_u32(raw, off);
    env.salt       = get_blob(raw, off, get_u32(raw, off));
    env.nonce      = get_blob(raw, off, get_u32(raw, off));
    env.ciphertext = get_blob(raw, off, get_u64(raw, off));
    env.tag        = get_blob(raw, off, get_u32(raw, off));
    env.magic = MAGIC;
    return env;
}

}  // namespace folioedit
