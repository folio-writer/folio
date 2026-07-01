// folioedit scaffold smoke test.
//
// Proves the module compiles, the locked shapes exist, and libcrypto is linked
// and reports OpenSSL 3.5.x. Does NOT call the stubbed engine bodies (seal,
// to_json, chain_hash) -- those throw until the engine build fills them.
//
// Build+run (bare, copy-paste as a block):
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include TEST_scaffold.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Seal_openssl.cpp -lcrypto -o test_scaffold && ./test_scaffold
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_scaffold.cpp ../src/Format.cpp ../src/Custody.cpp ../src/Seal_openssl.cpp -I "$OSSL/include" "$OSSL/libcrypto.a" -ldl -lpthread -o test_scaffold && ./test_scaffold
*/

#include "folioedit/Envelope.hpp"
#include "folioedit/Format.hpp"
#include "folioedit/Custody.hpp"
#include "folioedit/Seal.hpp"

#include <cassert>
#include <cstdio>
#include <string>

int main() {
    int pass = 0, total = 0;
    auto check = [&](const char* what, bool ok) {
        ++total;
        if (ok) { ++pass; }
        std::printf("  [%s] %s\n", ok ? "ok" : "XX", what);
    };

    // -- locked constants ------------------------------------------------------
    check("schema version is 1", folioedit::SCHEMA_VERSION == 1);
    check("magic is FOLIOEDIT", std::string(folioedit::MAGIC) == "FOLIOEDIT");
    check("cipher id AesGcm256 == 1",
          static_cast<int>(folioedit::CipherId::AesGcm256) == 1);

    // -- shapes default-construct ---------------------------------------------
    folioedit::Envelope env;
    check("envelope defaults to FOLIOEDIT/schema-1/AesGcm256",
          env.magic == "FOLIOEDIT" && env.schema == 1 &&
          env.cipher == folioedit::CipherId::AesGcm256);

    folioedit::Document doc;
    doc.pass.source = "jane";
    doc.scenes.push_back({"scn_k3f9", "The Ridge", 3, "<p>...</p>"});
    doc.annotations.push_back({"scn_k3f9", 33, 52, "watched the treeline",
                               "Proofreader", "filter verb"});
    doc.custody.push_back({});
    check("document holds body + annotations + custody",
          doc.scenes.size() == 1 && doc.annotations.size() == 1 &&
          doc.custody.size() == 1 && doc.pass.source == "jane");

    // -- libcrypto is linked and is 3.5.x -------------------------------------
    std::string backend = folioedit::seal_backend();
    std::printf("  backend: %s\n", backend.c_str());
    check("libcrypto linked, OpenSSL 3.5.x",
          backend.rfind("OpenSSL 3.5", 0) == 0);

    std::printf("\nfolioedit scaffold: %d/%d\n", pass, total);
    return pass == total ? 0 : 1;
}
