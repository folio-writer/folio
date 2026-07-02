# ─────────────────────────────────────────────────────────────────────────────
# folioedit -- the editorial-interchange engine (.folioedit format + custody).
#
# A pure STL static library (libfolioedit.a): the format, the hash-chained chain
# of custody, and the AES-256-GCM seal seam. Dependencies are STL + nlohmann +
# libcrypto ONLY -- deliberately no gtkmm and no spdlog, so the CLI target can
# link this bare later. The directory boundary enforces the law that the engine
# never links DocumentModel and never sees GTK. (DESIGN_editorialization s16.8.)
# ─────────────────────────────────────────────────────────────────────────────

# folioedit-plain: the s18 two-faces flag. ON omits the three OpenSSL TUs and
# the seal/sign/timestamp verbs (compiled out via FOLIOEDIT_NO_CRYPTO), leaving
# the pure core (Format, Custody, Sha256, KeyHex, Passphrase, Anchor, Vocabulary,
# Archive's plain read/write + hashes) + the CLI's info/annotate/del. Links NO
# OpenSSL, builds anywhere/any arch in seconds. A build FLAG on one codebase (not
# a separate target), so the two faces cannot drift. (s18.7.)
#   cmake -B build-plain -DFOLIOEDIT_NO_CRYPTO=ON -DFOLIOEDIT_BUILD_CLI=ON
option(FOLIOEDIT_NO_CRYPTO "Build the plain face: omit the OpenSSL TUs + seal/sign/timestamp verbs" OFF)

add_library(folioedit STATIC
    src/Format.cpp
    src/Custody.cpp
    src/Sha256.cpp         # pure-STL FIPS 180-4 SHA-256 (s18.4; replaces libcrypto's)
    src/KeyHex.cpp         # pure key<->hex codec (no crypto, no json)
    src/Passphrase.cpp     # pure phrase generator + canonicalizer (no crypto, no json)
    src/Anchor.cpp         # pure re-anchor ladder: offset -> quote -> floating (no crypto, no json)
    src/Vocabulary.cpp     # the shared command/kind vocabulary (pure data)
    src/Archive.cpp        # whole-file open/save orchestration + binds hashes
)

if(NOT FOLIOEDIT_NO_CRYPTO)
    target_sources(folioedit PRIVATE
        src/Seal_openssl.cpp       # AES-256-GCM seal + PBKDF2 KDF (includes <openssl/*>)
        src/Identity_openssl.cpp   # Ed25519 keypairs + event signing (includes <openssl/*>)
        src/Timestamp_openssl.cpp  # RFC-3161 trusted timestamps (includes <openssl/*>)
    )
else()
    # PUBLIC so the library, the CLI, and any consumer all compile out the sealed
    # API together -- the header guards key off this exact define.
    target_compile_definitions(folioedit PUBLIC FOLIOEDIT_NO_CRYPTO)
    message(STATUS "folioedit: PLAIN face (FOLIOEDIT_NO_CRYPTO) -- no OpenSSL, no seal/sign/timestamp")
endif()

target_include_directories(folioedit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(folioedit PUBLIC cxx_std_20)

# nlohmann -- reuse the target the root already provides (find_package or the
# FetchContent fallback). Header-only, so PUBLIC (consumers include it too).
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(folioedit PUBLIC nlohmann_json::nlohmann_json)
else()
    message(FATAL_ERROR "folioedit: nlohmann_json target not found -- expected the "
                        "root CMakeLists to provide it before add_subdirectory(folioedit).")
endif()

find_package(Threads REQUIRED)

# ── OpenSSL (libcrypto, static) ──────────────────────────────────────────────
# Skipped entirely for the plain face (FOLIOEDIT_NO_CRYPTO) -- the whole reason
# folioedit-plain builds anywhere in seconds is that this block does not run.
#
# Default (sealed): fetch + build a pinned OpenSSL 3.5.7 from source. OpenSSL is
# NOT a CMake project (it drives its own Configure/make), so this is
# ExternalProject, not FetchContent_MakeAvailable. Nothing is committed to the
# repo; a fresh clone builds it once (~1 min with -j), cached thereafter.
# libcrypto only -- no libssl (TLS, unused), no zlib.
#
# Override for local dev / an existing static build:
#   cmake -B build -DFOLIOEDIT_OPENSSL_ROOT=~/Develop/openssl
# links that build's libcrypto.a directly and skips the fetch entirely (instant).
if(NOT FOLIOEDIT_NO_CRYPTO)
set(FOLIOEDIT_OPENSSL_ROOT "" CACHE PATH
    "Existing static OpenSSL build (contains libcrypto.a + include/). Empty = fetch+build 3.5.7.")

if(FOLIOEDIT_OPENSSL_ROOT)
    message(STATUS "folioedit: linking OpenSSL from ${FOLIOEDIT_OPENSSL_ROOT} (override)")
    set(_FE_OSSL_INCLUDE "${FOLIOEDIT_OPENSSL_ROOT}/include")
    target_include_directories(folioedit PRIVATE "${_FE_OSSL_INCLUDE}")
    target_link_libraries(folioedit PRIVATE
        "${FOLIOEDIT_OPENSSL_ROOT}/libcrypto.a" ${CMAKE_DL_LIBS} Threads::Threads)
else()
    message(STATUS "folioedit: fetching + building OpenSSL 3.5.7 (ExternalProject)")
    include(ExternalProject)
    include(ProcessorCount)
    ProcessorCount(_FE_NPROC)
    if(_FE_NPROC EQUAL 0)
        set(_FE_NPROC 1)
    endif()

    ExternalProject_Add(folioedit_openssl
        URL      https://github.com/openssl/openssl/releases/download/openssl-3.5.7/openssl-3.5.7.tar.gz
        URL_HASH SHA256=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
        # OpenSSL's ./config auto-detects the platform (portable across contributors);
        # no-shared -> static libcrypto.a; drop tests/apps/docs for a lean, fast build.
        CONFIGURE_COMMAND <SOURCE_DIR>/config no-shared no-tests no-apps no-docs
                          --prefix=<INSTALL_DIR> --libdir=lib
        BUILD_COMMAND     make -j${_FE_NPROC}
        INSTALL_COMMAND   make -j${_FE_NPROC} install_sw
        BUILD_IN_SOURCE   1
        BUILD_BYPRODUCTS  <INSTALL_DIR>/lib/libcrypto.a
    )
    ExternalProject_Get_Property(folioedit_openssl INSTALL_DIR)
    # The include dir must exist at configure time for target_include_directories;
    # the real headers are populated by the build step below.
    file(MAKE_DIRECTORY "${INSTALL_DIR}/include")

    add_dependencies(folioedit folioedit_openssl)
    set(_FE_OSSL_INCLUDE "${INSTALL_DIR}/include")
    target_include_directories(folioedit PRIVATE "${_FE_OSSL_INCLUDE}")
    target_link_libraries(folioedit PRIVATE
        "${INSTALL_DIR}/lib/libcrypto.a" ${CMAKE_DL_LIBS} Threads::Threads)
endif()
endif()  # NOT FOLIOEDIT_NO_CRYPTO

# ── Sandbox tests (opt-in) ───────────────────────────────────────────────────
# Off by default so the normal Folio build doesn't compile them. Enable with
#   cmake -B build -DFOLIOEDIT_BUILD_TESTS=ON
option(FOLIOEDIT_BUILD_TESTS "Build folioedit sandbox tests" OFF)
if(FOLIOEDIT_BUILD_TESTS)
    # ── pure tests (build in BOTH faces; link no libcrypto) ──────────────────
    add_executable(test_folioedit_format tests/TEST_format.cpp)
    target_link_libraries(test_folioedit_format PRIVATE folioedit)

    add_executable(test_folioedit_custody tests/TEST_custody.cpp)
    target_link_libraries(test_folioedit_custody PRIVATE folioedit)

    # The s18.4 equivalence gate: pure SHA-256 == libcrypto, FIPS vectors +
    # pinned s102 fixture/chain hashes. Links only the pure TUs.
    add_executable(test_folioedit_sha256 tests/TEST_sha256.cpp)
    target_link_libraries(test_folioedit_sha256 PRIVATE folioedit)

    # The s18 plain face: readable JSON round-trip, content sniff, body_hash
    # integrity (only-appended proof) -- all pure, builds under NO_CRYPTO.
    add_executable(test_folioedit_archive_plain tests/TEST_archive_plain.cpp)
    target_link_libraries(test_folioedit_archive_plain PRIVATE folioedit)

    add_executable(test_folioedit_keyhex tests/TEST_keyhex.cpp)
    target_link_libraries(test_folioedit_keyhex PRIVATE folioedit)

    add_executable(test_folioedit_vocab tests/TEST_vocab.cpp)
    target_link_libraries(test_folioedit_vocab PRIVATE folioedit)

    add_executable(test_folioedit_passphrase tests/TEST_passphrase.cpp)
    target_link_libraries(test_folioedit_passphrase PRIVATE folioedit)

    add_executable(test_folioedit_anchor tests/TEST_anchor.cpp)
    target_link_libraries(test_folioedit_anchor PRIVATE folioedit)

    # ── sealed-face tests (need libcrypto; skipped under FOLIOEDIT_NO_CRYPTO) ─
    if(NOT FOLIOEDIT_NO_CRYPTO)
        add_executable(test_folioedit_scaffold tests/TEST_scaffold.cpp)
        target_link_libraries(test_folioedit_scaffold PRIVATE folioedit)

        add_executable(test_folioedit_seal tests/TEST_seal.cpp)
        target_link_libraries(test_folioedit_seal PRIVATE folioedit)

        add_executable(test_folioedit_kdf tests/TEST_kdf.cpp)
        target_link_libraries(test_folioedit_kdf PRIVATE folioedit)

        add_executable(test_folioedit_identity tests/TEST_identity.cpp)
        target_link_libraries(test_folioedit_identity PRIVATE folioedit)

        add_executable(test_folioedit_timestamp tests/TEST_timestamp.cpp)
        target_link_libraries(test_folioedit_timestamp PRIVATE folioedit)
        # This test alone includes <openssl/*> directly (it stands up a local TSA),
        # so it needs the OpenSSL headers the library keeps PRIVATE.
        target_include_directories(test_folioedit_timestamp PRIVATE "${_FE_OSSL_INCLUDE}")

        add_executable(test_folioedit_archive tests/TEST_archive.cpp)
        target_link_libraries(test_folioedit_archive PRIVATE folioedit)
    endif()
endif()

# ── CLI (opt-in) ─────────────────────────────────────────────────────────────
# The step-6 face: links libfolioedit bare (STL + libcrypto, no gtk). Off by
# default so a normal Folio build doesn't produce it.
#   cmake -B build -DFOLIOEDIT_BUILD_CLI=ON
option(FOLIOEDIT_BUILD_CLI "Build the folioedit CLI" OFF)
if(FOLIOEDIT_BUILD_CLI)
    add_executable(folioedit_cli cli/main.cpp)
    set_target_properties(folioedit_cli PROPERTIES OUTPUT_NAME folioedit)
    target_link_libraries(folioedit_cli PRIVATE folioedit)
endif()
