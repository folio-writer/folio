// folioedit :: Timestamp tests -- RFC-3161 request construction, and a full
// offline round-trip against a LOCAL TSA stood up in-test (self-signed cert with
// the timeStamping EKU + TS_RESP_CTX). Proves fetch -> verify -> extract-time,
// the import re-verify, imprint/trust/tamper rejection, and the degrade-to-local
// path -- all without a network. The only thing left for Fedora/online is
// pointing the transport at a real public TSA. Needs libcrypto.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_timestamp.cpp ../src/Timestamp_openssl.cpp ../src/Identity_openssl.cpp ../src/Custody.cpp ../src/KeyHex.cpp ../src/Format.cpp -lcrypto -o test_timestamp && ./test_timestamp
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include -I "$OSSL/include" TEST_timestamp.cpp ../src/Timestamp_openssl.cpp ../src/Identity_openssl.cpp ../src/Custody.cpp ../src/KeyHex.cpp ../src/Format.cpp "$OSSL/libcrypto.a" -ldl -lpthread -o test_timestamp && ./test_timestamp
*/

#include "folioedit/Timestamp.hpp"
#include "folioedit/Identity.hpp"
#include "folioedit/Custody.hpp"
#include "folioedit/Format.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/pem.h>
#include <openssl/ts.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace fe = folioedit;

static int g_pass = 0, g_total = 0;
static void check(const char* what, bool ok) {
    ++g_total;
    if (ok) ++g_pass;
    std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
}
static bool threw(const std::function<void()>& f) {
    try { f(); return false; } catch (const std::exception&) { return true; }
}

// ── a local TSA (self-signed, timeStamping EKU) ──────────────────────────────
struct LocalTSA {
    EVP_PKEY* key  = nullptr;
    X509*     cert = nullptr;
    ~LocalTSA() { X509_free(cert); EVP_PKEY_free(key); }
};

static ASN1_INTEGER* serial_cb(TS_RESP_CTX*, void*) {
    ASN1_INTEGER* s = ASN1_INTEGER_new();
    ASN1_INTEGER_set(s, 42);
    return s;
}

static void build_tsa(LocalTSA& tsa) {
    tsa.key = EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<size_t>(2048));
    if (!tsa.key) throw std::runtime_error("keygen failed");

    tsa.cert = X509_new();
    X509_set_version(tsa.cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(tsa.cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(tsa.cert), -3600);
    X509_gmtime_adj(X509_getm_notAfter(tsa.cert), 60L * 60L * 24L);
    X509_set_pubkey(tsa.cert, tsa.key);

    X509_NAME* nm = X509_get_subject_name(tsa.cert);
    X509_NAME_add_entry_by_txt(nm, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("folioedit test TSA"), -1, -1, 0);
    X509_set_issuer_name(tsa.cert, nm);   // self-signed

    X509V3_CTX vc;
    X509V3_set_ctx(&vc, tsa.cert, tsa.cert, nullptr, nullptr, 0);
    // timeStamping EKU (critical) is required of a TSA signer.
    X509_EXTENSION* eku = X509V3_EXT_conf_nid(nullptr, &vc, NID_ext_key_usage,
                                              "critical,timeStamping");
    X509_add_ext(tsa.cert, eku, -1);
    X509_EXTENSION_free(eku);
    X509_EXTENSION* bc = X509V3_EXT_conf_nid(nullptr, &vc, NID_basic_constraints,
                                             "critical,CA:FALSE");
    X509_add_ext(tsa.cert, bc, -1);
    X509_EXTENSION_free(bc);

    if (X509_sign(tsa.cert, tsa.key, EVP_sha256()) == 0)
        throw std::runtime_error("cert sign failed");
}

static std::string cert_pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(bio, &bptr);
    std::string pem(bptr->data, bptr->length);
    BIO_free(bio);
    return pem;
}

// The injected transport: a local TSA answering the posted request.
static fe::ts_bytes tsa_transport(LocalTSA& tsa, const fe::ts_bytes& tsq_der) {
    TS_RESP_CTX* ctx = TS_RESP_CTX_new();
    if (!ctx) throw std::runtime_error("TS_RESP_CTX_new failed");
    TS_RESP_CTX_set_signer_cert(ctx, tsa.cert);
    TS_RESP_CTX_set_signer_key(ctx, tsa.key);
    ASN1_OBJECT* policy = OBJ_txt2obj("1.2.3.4.1", 1);
    TS_RESP_CTX_set_def_policy(ctx, policy);
    ASN1_OBJECT_free(policy);
    TS_RESP_CTX_add_md(ctx, EVP_sha256());
    TS_RESP_CTX_set_serial_cb(ctx, serial_cb, nullptr);

    BIO* in = BIO_new_mem_buf(tsq_der.data(), static_cast<int>(tsq_der.size()));
    TS_RESP* resp = TS_RESP_create_response(ctx, in);
    fe::ts_bytes out;
    if (resp) {
        unsigned char* der = nullptr;
        int len = i2d_TS_RESP(resp, &der);
        if (len > 0) { out.assign(der, der + len); OPENSSL_free(der); }
        TS_RESP_free(resp);
    }
    BIO_free(in);
    TS_RESP_CTX_free(ctx);
    if (out.empty()) throw std::runtime_error("TSA produced no response");
    return out;
}

static void test_request() {
    const std::string h = fe::sha256_hex("the event to be stamped");
    fe::TimestampRequest req = fe::make_timestamp_request(h);
    check("request DER is non-empty", !req.der.empty());
    check("request carries a nonce",  !req.nonce.empty());

    // Re-parse the DER and confirm the imprint bytes == our hash.
    const unsigned char* p = req.der.data();
    TS_REQ* r = d2i_TS_REQ(nullptr, &p, static_cast<long>(req.der.size()));
    check("request DER re-parses as a TS_REQ", r != nullptr);
    if (r) {
        TS_MSG_IMPRINT*    mi = TS_REQ_get_msg_imprint(r);
        ASN1_OCTET_STRING* os = TS_MSG_IMPRINT_get_msg(mi);
        fe::ts_bytes hash;
        for (std::size_t i = 0; i < h.size(); i += 2)
            hash.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
        bool match = ASN1_STRING_length(os) == static_cast<int>(hash.size()) &&
                     std::memcmp(ASN1_STRING_get0_data(os), hash.data(), hash.size()) == 0;
        check("imprint == the event hash", match);
        check("cert_req is set", TS_REQ_get_cert_req(r) == 1);
        TS_REQ_free(r);
    }
}

static void test_roundtrip(LocalTSA& tsa) {
    const std::string pem = cert_pem(tsa.cert);
    const std::string h   = fe::sha256_hex("stamp me at a trusted time");
    auto transport = [&](const fe::ts_bytes& q){ return tsa_transport(tsa, q); };

    fe::TimestampProof proof = fe::fetch_timestamp(h, transport, pem);
    check("fetch returns a token", !proof.token_b64.empty());
    check("fetch returns an ISO time (…Z)",
          proof.time_iso.size() >= 20 && proof.time_iso.back() == 'Z');

    // Re-verify the stored token on "import" against the same hash + trust.
    std::string t = fe::verify_timestamp_token(proof.token_b64, h, pem);
    check("stored token re-verifies", t == proof.time_iso);

    // Imprint mismatch: a different hash must fail.
    check("token verify rejects a different hash",
          threw([&]{ fe::verify_timestamp_token(proof.token_b64,
                       fe::sha256_hex("a different event"), pem); }));

    // Untrusted: an unrelated cert as the anchor must fail.
    LocalTSA other; build_tsa(other);
    check("token verify rejects an untrusted anchor",
          threw([&]{ fe::verify_timestamp_token(proof.token_b64, h, cert_pem(other.cert)); }));

    // Tampered token bytes must fail.
    std::string bad = proof.token_b64;
    bad[bad.size() / 2] = (bad[bad.size() / 2] == 'A' ? 'B' : 'A');
    check("tampered token is rejected",
          threw([&]{ fe::verify_timestamp_token(bad, h, pem); }));
}

static void test_event_paths(LocalTSA& tsa) {
    const std::string pem = cert_pem(tsa.cert);
    fe::KeyPair editor = fe::generate_keypair();

    // A signed, finalized event.
    fe::CustodyEvent e;
    e.seq = 0; e.kind = fe::CustodyEvent_Kind::Sealed;
    e.actor = "jane"; e.actor_id = fe::fingerprint(editor.public_key);
    e.at = "2026-07-01T12:00:00Z";
    e.binds = fe::sha256_hex("annotations coming back");
    fe::finalize_event(e, "");
    fe::sign_event(e, editor);

    const std::string hash_before = e.hash;
    const std::string sig_before  = e.signature;
    const std::string at_before   = e.at;

    // Success path: attach a trusted token.
    auto good = [&](const fe::ts_bytes& q){ return tsa_transport(tsa, q); };
    bool ok = fe::try_timestamp_event(e, good, pem);
    check("try_timestamp_event succeeds against the TSA", ok);
    check("time_source is now Rfc3161", e.time_source == fe::TimeSource::Rfc3161);
    check("a token was attached", !e.timestamp_token.empty());
    check("finalize fields untouched (hash/sig/at)",
          e.hash == hash_before && e.signature == sig_before && e.at == at_before);
    check("chain still verifies after timestamping", fe::verify_chain({e}));
    check("signature still verifies after timestamping",
          fe::verify_event_signature(e, editor.public_key));
    check("attached token verifies against the event hash",
          !fe::verify_timestamp_token(e.timestamp_token, e.hash, pem).empty());

    // Degrade path: transport down -> local clock, no token, nothing blocks.
    fe::CustodyEvent e2 = e;
    e2.time_source = fe::TimeSource::Rfc3161;   // pretend a prior value
    e2.timestamp_token = "stale";
    auto offline = [](const fe::ts_bytes&) -> fe::ts_bytes {
        throw std::runtime_error("TSA unreachable");
    };
    bool ok2 = fe::try_timestamp_event(e2, offline, pem);
    check("try_timestamp_event degrades when offline", !ok2);
    check("degrade sets LocalClock + clears token",
          e2.time_source == fe::TimeSource::LocalClock && e2.timestamp_token.empty());
}

// Full lifecycle through JSON: actor -> finalize -> sign -> timestamp -> to_json
// -> from_json, then chain + signature + timestamp all still verify.
static void test_capstone(LocalTSA& tsa) {
    const std::string pem = cert_pem(tsa.cert);
    fe::KeyPair editor = fe::generate_keypair();
    auto transport = [&](const fe::ts_bytes& q){ return tsa_transport(tsa, q); };

    fe::CustodyEvent e;
    e.seq = 0; e.kind = fe::CustodyEvent_Kind::Issued;
    e.actor = "scott"; e.actor_id = fe::fingerprint(editor.public_key);
    e.at = "2026-07-01T00:00:00Z";
    e.binds = fe::sha256_hex("scenes out");
    fe::finalize_event(e, "");
    fe::sign_event(e, editor);
    fe::try_timestamp_event(e, transport, pem);

    fe::Document doc; doc.project_id = "prj_ts"; doc.custody.push_back(e);
    fe::Document back = fe::from_json(fe::to_json(doc));

    check("custody survived JSON", back.custody.size() == 1);
    const fe::CustodyEvent& r = back.custody[0];
    check("chain verifies after JSON", fe::verify_chain(back.custody));
    check("signature verifies after JSON", fe::verify_event_signature(r, editor.public_key));
    check("timestamp token verifies after JSON",
          r.time_source == fe::TimeSource::Rfc3161 &&
          !fe::verify_timestamp_token(r.timestamp_token, r.hash, pem).empty());
}

int main() {
    std::printf("folioedit Timestamp tests\n");
    LocalTSA tsa; build_tsa(tsa);
    test_request();
    test_roundtrip(tsa);
    test_event_paths(tsa);
    test_capstone(tsa);
    std::printf("\nfolioedit timestamp: %d/%d\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
