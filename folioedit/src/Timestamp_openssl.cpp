//
// folioedit :: Timestamp_openssl -- RFC-3161 trusted timestamps via libcrypto
// ts.h. Impl-side companion to Timestamp.hpp; one of the <openssl/*> TUs.
//
// The engine builds the request and verifies the token; the HTTP POST to the TSA
// is the caller's injected transport (no libcurl here). Verification is honest:
// the token's CMS signature is checked against a caller-supplied trust anchor
// (the TSA cert/CA), the message imprint is compared to the event hash, and (on
// the request path) the nonce is checked -- so a forged or swapped token fails.
//
// Non-deprecated EVP/TS throughout. Ownership is fiddly in the TS API (several
// setters take ownership); noted at each transfer.
//
#include "folioedit/Timestamp.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <stdexcept>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/rand.h>
#include <openssl/ts.h>
#include <openssl/x509.h>

namespace folioedit {
namespace {

constexpr std::size_t HASH_BYTES = 32;   // SHA-256 event hash
constexpr int         NONCE_BYTES = 16;

// ── hex (event hash-hex -> 32 raw bytes) ─────────────────────────────────────
int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
ts_bytes decode_hash(const std::string& hex) {
    if (hex.size() != HASH_BYTES * 2)
        throw std::runtime_error("folioedit: timestamp hash must be 64 hex chars");
    ts_bytes out;
    out.reserve(HASH_BYTES);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            throw std::runtime_error("folioedit: timestamp hash has a non-hex char");
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

// ── base64 (no newlines) via BIO ─────────────────────────────────────────────
std::string b64_encode(const ts_bytes& in) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    if (!in.empty())
        BIO_write(b64, in.data(), static_cast<int>(in.size()));
    (void)BIO_flush(b64);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string out(bptr->data, bptr->length);
    BIO_free_all(b64);
    return out;
}
ts_bytes b64_decode(const std::string& s) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new_mem_buf(s.data(), static_cast<int>(s.size()));
    mem = BIO_push(b64, mem);
    ts_bytes out(s.size());   // decoded length <= input length
    int n = BIO_read(mem, out.data(), static_cast<int>(out.size()));
    if (n < 0) n = 0;
    out.resize(static_cast<std::size_t>(n));
    BIO_free_all(mem);
    return out;
}

// ── ASN.1 GeneralizedTime -> ISO-8601 UTC ────────────────────────────────────
std::string gentime_iso(const ASN1_GENERALIZEDTIME* gt) {
    if (!gt) throw std::runtime_error("folioedit: timestamp token has no time");
    struct tm tmv;
    std::memset(&tmv, 0, sizeof tmv);
    if (ASN1_TIME_to_tm(reinterpret_cast<const ASN1_TIME*>(gt), &tmv) != 1)
        throw std::runtime_error("folioedit: cannot parse timestamp time");
    char buf[80];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

// ── trust store from PEM text ────────────────────────────────────────────────
// Returns a raw X509_STORE the caller transfers to a TS_VERIFY_CTX (which then
// owns it). Throws if no certs parse.
X509_STORE* store_from_pem(const std::string& pem) {
    X509_STORE* store = X509_STORE_new();
    if (!store) throw std::runtime_error("folioedit: X509_STORE_new failed");
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    int added = 0;
    X509* cert = nullptr;
    while ((cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        X509_STORE_add_cert(store, cert);   // bumps ref; we still own our cert
        X509_free(cert);
        ++added;
    }
    BIO_free(bio);
    if (added == 0) {
        X509_STORE_free(store);
        throw std::runtime_error("folioedit: no trust certs in PEM");
    }
    return store;
}

// ── build a TS_REQ over a 32-byte hash (owned by caller) ─────────────────────
TS_REQ* build_request(const ts_bytes& hash32, ts_bytes& nonce_out) {
    TS_REQ* req = TS_REQ_new();
    if (!req) throw std::runtime_error("folioedit: TS_REQ_new failed");
    TS_REQ_set_version(req, 1);

    TS_MSG_IMPRINT* imp  = TS_MSG_IMPRINT_new();
    X509_ALGOR*     algo = X509_ALGOR_new();
    // SHA-256 OID is a static object; X509_ALGOR_free no-ops on it.
    X509_ALGOR_set0(algo, OBJ_nid2obj(NID_sha256), V_ASN1_NULL, nullptr);
    TS_MSG_IMPRINT_set_algo(imp, algo);   // dups algo
    TS_MSG_IMPRINT_set_msg(imp, const_cast<unsigned char*>(hash32.data()),
                           static_cast<int>(hash32.size()));   // copies
    TS_REQ_set_msg_imprint(req, imp);     // dups imp
    X509_ALGOR_free(algo);
    TS_MSG_IMPRINT_free(imp);

    nonce_out.resize(static_cast<std::size_t>(NONCE_BYTES));
    if (RAND_bytes(nonce_out.data(), NONCE_BYTES) != 1) {
        TS_REQ_free(req);
        throw std::runtime_error("folioedit: RAND_bytes (nonce) failed");
    }
    BIGNUM*       bn    = BN_bin2bn(nonce_out.data(), NONCE_BYTES, nullptr);
    ASN1_INTEGER* nonce = BN_to_ASN1_INTEGER(bn, nullptr);
    TS_REQ_set_nonce(req, nonce);         // dups nonce
    ASN1_INTEGER_free(nonce);
    BN_free(bn);

    TS_REQ_set_cert_req(req, 1);          // TSA includes its cert in the token
    return req;
}

using ReqPtr   = std::unique_ptr<TS_REQ, decltype(&TS_REQ_free)>;
using RespPtr  = std::unique_ptr<TS_RESP, decltype(&TS_RESP_free)>;
using VctxPtr  = std::unique_ptr<TS_VERIFY_CTX, decltype(&TS_VERIFY_CTX_free)>;
using P7Ptr    = std::unique_ptr<PKCS7, decltype(&PKCS7_free)>;
using InfoPtr  = std::unique_ptr<TS_TST_INFO, decltype(&TS_TST_INFO_free)>;

// Give a verify ctx ownership of a trust store. The old TS_VERIFY_CTX_set_store
// is deprecated in OpenSSL 3.4 for the same-semantics set0_store; branch by
// version so this stays -Werror-clean on both 3.0.x and 3.4+.
void vctx_own_store(TS_VERIFY_CTX* ctx, X509_STORE* store) {
#if OPENSSL_VERSION_NUMBER >= 0x30400000L
    TS_VERIFY_CTX_set0_store(ctx, store);
#else
    TS_VERIFY_CTX_set_store(ctx, store);
#endif
}

}  // namespace

TimestampRequest make_timestamp_request(const std::string& hash_hex) {
    ts_bytes hash32 = decode_hash(hash_hex);
    ts_bytes nonce;
    ReqPtr req(build_request(hash32, nonce), &TS_REQ_free);

    unsigned char* der = nullptr;
    int derlen = i2d_TS_REQ(req.get(), &der);
    if (derlen <= 0) throw std::runtime_error("folioedit: i2d_TS_REQ failed");
    ts_bytes out(der, der + derlen);
    OPENSSL_free(der);

    return TimestampRequest{std::move(out), std::move(nonce)};
}

TimestampProof fetch_timestamp(const std::string& hash_hex,
                               const TimestampTransport& transport,
                               const std::string& trust_pem) {
    ts_bytes hash32 = decode_hash(hash_hex);
    ts_bytes nonce;
    ReqPtr req(build_request(hash32, nonce), &TS_REQ_free);

    unsigned char* der = nullptr;
    int derlen = i2d_TS_REQ(req.get(), &der);
    if (derlen <= 0) throw std::runtime_error("folioedit: i2d_TS_REQ failed");
    ts_bytes tsq(der, der + derlen);
    OPENSSL_free(der);

    ts_bytes tsr = transport(tsq);   // the online seam
    if (tsr.empty()) throw std::runtime_error("folioedit: empty TSR (TSA unreachable)");

    const unsigned char* p = tsr.data();
    RespPtr resp(d2i_TS_RESP(nullptr, &p, static_cast<long>(tsr.size())), &TS_RESP_free);
    if (!resp) throw std::runtime_error("folioedit: cannot parse TSR");

    // Verify ctx from the request carries imprint + nonce; add signature + store.
    VctxPtr vctx(TS_REQ_to_TS_VERIFY_CTX(req.get(), nullptr), &TS_VERIFY_CTX_free);
    if (!vctx) throw std::runtime_error("folioedit: TS_REQ_to_TS_VERIFY_CTX failed");
    TS_VERIFY_CTX_add_flags(vctx.get(), TS_VFY_SIGNATURE);
    vctx_own_store(vctx.get(), store_from_pem(trust_pem));   // vctx owns store

    if (TS_RESP_verify_response(vctx.get(), resp.get()) != 1)
        throw std::runtime_error("folioedit: timestamp response failed verification");

    TS_TST_INFO* info = TS_RESP_get_tst_info(resp.get());   // owned by resp
    std::string iso = gentime_iso(TS_TST_INFO_get_time(info));

    PKCS7* token = TS_RESP_get_token(resp.get());            // owned by resp
    unsigned char* td = nullptr;
    int tl = i2d_PKCS7(token, &td);
    if (tl <= 0) throw std::runtime_error("folioedit: i2d_PKCS7 (token) failed");
    ts_bytes tb(td, td + tl);
    OPENSSL_free(td);

    return TimestampProof{std::move(iso), b64_encode(tb)};
}

std::string verify_timestamp_token(const std::string& token_b64,
                                   const std::string& hash_hex,
                                   const std::string& trust_pem) {
    ts_bytes hash32 = decode_hash(hash_hex);
    ts_bytes der    = b64_decode(token_b64);
    if (der.empty()) throw std::runtime_error("folioedit: empty timestamp token");

    const unsigned char* p = der.data();
    P7Ptr token(d2i_PKCS7(nullptr, &p, static_cast<long>(der.size())), &PKCS7_free);
    if (!token) throw std::runtime_error("folioedit: cannot parse timestamp token");

    // Signature + trust only (no request/nonce on import); imprint checked by hand.
    VctxPtr vctx(TS_VERIFY_CTX_new(), &TS_VERIFY_CTX_free);
    if (!vctx) throw std::runtime_error("folioedit: TS_VERIFY_CTX_new failed");
    TS_VERIFY_CTX_set_flags(vctx.get(), TS_VFY_VERSION | TS_VFY_SIGNATURE);
    vctx_own_store(vctx.get(), store_from_pem(trust_pem));   // vctx owns store

    if (TS_RESP_verify_token(vctx.get(), token.get()) != 1)
        throw std::runtime_error("folioedit: timestamp token failed verification");

    InfoPtr info(PKCS7_to_TS_TST_INFO(token.get()), &TS_TST_INFO_free);
    if (!info) throw std::runtime_error("folioedit: no TSTInfo in token");

    TS_MSG_IMPRINT*    mi = TS_TST_INFO_get_msg_imprint(info.get());
    ASN1_OCTET_STRING* os = TS_MSG_IMPRINT_get_msg(mi);
    const int   ilen = ASN1_STRING_length(os);
    const auto* idat = ASN1_STRING_get0_data(os);
    if (ilen != static_cast<int>(hash32.size()) ||
        std::memcmp(idat, hash32.data(), hash32.size()) != 0)
        throw std::runtime_error("folioedit: timestamp imprint != event hash");

    return gentime_iso(TS_TST_INFO_get_time(info.get()));
}

bool try_timestamp_event(CustodyEvent& e, const TimestampTransport& transport,
                         const std::string& trust_pem) {
    if (e.hash.empty()) return false;   // not finalized -- nothing to certify
    try {
        TimestampProof proof = fetch_timestamp(e.hash, transport, trust_pem);
        e.time_source     = TimeSource::Rfc3161;
        e.timestamp_token = proof.token_b64;   // both excluded from the hash
        return true;
    } catch (const std::exception&) {
        e.time_source     = TimeSource::LocalClock;
        e.timestamp_token.clear();
        return false;   // degrade, never block
    }
}

}  // namespace folioedit
