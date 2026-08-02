# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(
    PrimeForge
    VERSION 0.1.0
    DESCRIPTION "Auditable prime-search, proof, and verification research platform"
    LANGUAGES CXX
)

include(CTest)

# CMake 4.3 can double-encode a non-breaking space while detecting localized
# MSVC /showIncludes output. Ninja then records zero header dependencies. Repair
# only that byte sequence and preserve every correctly detected locale prefix.
if(MSVC AND CMAKE_GENERATOR MATCHES "^Ninja")
    string(ASCII 194 160 primeforge_nbsp)
    string(ASCII 195 130 194 160 primeforge_double_encoded_nbsp)
    string(REPLACE
        "${primeforge_double_encoded_nbsp}"
        "${primeforge_nbsp}"
        primeforge_showincludes_prefix
        "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}"
    )
    if(NOT primeforge_showincludes_prefix STREQUAL CMAKE_CXX_CL_SHOWINCLUDES_PREFIX)
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${primeforge_showincludes_prefix}")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${primeforge_showincludes_prefix}")
        message(STATUS "Normalized localized MSVC /showIncludes prefix for Ninja")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(primeforge_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

set(primeforge_family_sieve_sources
    src/family_sieve/family_sieve.cpp
    src/family_sieve/vector_scalar.cpp
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|x86|i[3-6]86)$")
    list(APPEND primeforge_family_sieve_sources
        src/family_sieve/vector_avx2.cpp
        src/family_sieve/vector_avx512.cpp
    )
    if(MSVC)
        set_source_files_properties(
            src/family_sieve/vector_avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
        )
        set_source_files_properties(
            src/family_sieve/vector_avx512.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX512"
        )
    else()
        set_source_files_properties(
            src/family_sieve/vector_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2"
        )
        set_source_files_properties(
            src/family_sieve/vector_avx512.cpp PROPERTIES COMPILE_OPTIONS "-mavx512f"
        )
    endif()
else()
    message(FATAL_ERROR "Stage-10 vector experiment currently requires an x86 or x86-64 build host")
endif()

add_library(primeforge_core STATIC
    src/congruence/compiler.cpp
    src/core/sha256.cpp
    src/core/status.cpp
    src/core/system_info.cpp
    src/engine/external_adapter.cpp
    src/family/family.cpp
    src/math/big_integer.cpp
    src/math/mul128.cpp
    src/sieve/sieve.cpp
    src/work/work_unit.cpp
    ${primeforge_family_sieve_sources}
)
add_library(PrimeForge::core ALIAS primeforge_core)

target_include_directories(primeforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(primeforge_core PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_core)

add_library(primeforge_benchmark STATIC src/benchmark/benchmark.cpp)
add_library(PrimeForge::benchmark ALIAS primeforge_benchmark)
target_include_directories(primeforge_benchmark
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(primeforge_benchmark PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_benchmark)

add_library(primeforge_adaptive STATIC src/adaptive_bound/adaptive_bound.cpp)
add_library(PrimeForge::adaptive ALIAS primeforge_adaptive)
target_include_directories(primeforge_adaptive
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(primeforge_adaptive PUBLIC PrimeForge::core PrimeForge::benchmark)
target_compile_features(primeforge_adaptive PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_adaptive)

if(WIN32)
    target_compile_definitions(primeforge_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(primeforge_core PRIVATE user32)
endif()

add_executable(primeforge-selftest src/selftest/main.cpp)
target_link_libraries(primeforge-selftest PRIVATE PrimeForge::core)
target_compile_features(primeforge-selftest PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-selftest)

add_executable(primeforge-tests tests/test_main.cpp)
target_link_libraries(primeforge-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-tests)

add_executable(primeforge-corpus-tests tests/corpus_test.cpp)
target_compile_features(primeforge-corpus-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-corpus-tests)

add_executable(primeforge-benchmark src/benchmark/main.cpp)
target_link_libraries(primeforge-benchmark PRIVATE PrimeForge::benchmark)
target_compile_features(primeforge-benchmark PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-benchmark)

add_executable(primeforge-benchmark-tests tests/benchmark_test.cpp)
target_link_libraries(primeforge-benchmark-tests PRIVATE PrimeForge::benchmark)
target_compile_features(primeforge-benchmark-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-benchmark-tests)

add_executable(primeforge-sieve src/sieve/main.cpp)
target_link_libraries(primeforge-sieve PRIVATE PrimeForge::core)
target_compile_features(primeforge-sieve PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-sieve)

add_executable(primeforge-sieve-tests tests/sieve_test.cpp)
target_link_libraries(primeforge-sieve-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-sieve-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-sieve-tests)

add_executable(primeforge-work-unit-tests tests/work_unit_test.cpp)
target_link_libraries(primeforge-work-unit-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-work-unit-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-work-unit-tests)

add_executable(primeforge-work-units src/work/main.cpp)
target_link_libraries(primeforge-work-units PRIVATE PrimeForge::core)
target_compile_features(primeforge-work-units PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-work-units)

add_executable(primeforge-family-tests tests/family_test.cpp)
target_link_libraries(primeforge-family-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-family-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-family-tests)

add_executable(primeforge-family src/family/main.cpp)
target_link_libraries(primeforge-family PRIVATE PrimeForge::core)
target_compile_features(primeforge-family PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-family)

add_executable(primeforge-congruence-tests tests/congruence_test.cpp)
target_link_libraries(primeforge-congruence-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-congruence-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-congruence-tests)

add_executable(primeforge-congruence src/congruence/main.cpp)
target_link_libraries(primeforge-congruence PRIVATE PrimeForge::core)
target_compile_features(primeforge-congruence PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-congruence)

add_executable(primeforge-family-sieve-tests tests/family_sieve_test.cpp)
target_link_libraries(primeforge-family-sieve-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-family-sieve-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-family-sieve-tests)

add_executable(primeforge-family-sieve-benchmark src/family_sieve/main.cpp)
target_link_libraries(
    primeforge-family-sieve-benchmark PRIVATE PrimeForge::core PrimeForge::benchmark
)
target_compile_features(primeforge-family-sieve-benchmark PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-family-sieve-benchmark)

add_executable(primeforge-adaptive-bound-tests tests/adaptive_bound_test.cpp)
target_link_libraries(primeforge-adaptive-bound-tests PRIVATE PrimeForge::adaptive)
target_compile_features(primeforge-adaptive-bound-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-adaptive-bound-tests)

add_executable(primeforge-adaptive-bound-benchmark src/adaptive_bound/main.cpp)
target_link_libraries(primeforge-adaptive-bound-benchmark PRIVATE PrimeForge::adaptive)
target_compile_features(primeforge-adaptive-bound-benchmark PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-adaptive-bound-benchmark)

add_executable(primeforge-adapter-fixture tests/adapter_fixture.cpp)
target_compile_features(primeforge-adapter-fixture PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-adapter-fixture)

add_executable(primeforge-external-adapter-tests tests/external_adapter_test.cpp)
target_link_libraries(primeforge-external-adapter-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-external-adapter-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-external-adapter-tests)

add_executable(primeforge-adapters src/engine/main.cpp)
target_link_libraries(primeforge-adapters PRIVATE PrimeForge::core)
target_compile_features(primeforge-adapters PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-adapters)

if(BUILD_TESTING)
    add_test(NAME primeforge.unit COMMAND primeforge-tests)
    add_test(NAME primeforge.selftest COMMAND primeforge-selftest)
    add_test(
        NAME primeforge.corpus
        COMMAND primeforge-corpus-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/cases.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/oracles/oracle_results.tsv
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/certificates/pari-primecert-valid.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/certificates/pari-primecert-corrupt.txt
    )
    add_test(NAME primeforge.benchmark_protocol COMMAND primeforge-benchmark-tests)
    add_test(
        NAME primeforge.sieve
        COMMAND primeforge-sieve-tests ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/cases.tsv
    )
    add_test(NAME primeforge.work_units COMMAND primeforge-work-unit-tests)
    add_test(NAME primeforge.family_language COMMAND primeforge-family-tests)
    add_test(
        NAME primeforge.family_cli
        COMMAND primeforge-family
            --definition ${CMAKE_CURRENT_SOURCE_DIR}/examples/families/proth_like.pf
            --set k=3
            --set n=5
            --modulus 11
    )
    add_test(NAME primeforge.congruence_compiler COMMAND primeforge-congruence-tests)
    add_test(NAME primeforge.family_sieve COMMAND primeforge-family-sieve-tests)
    add_test(NAME primeforge.adaptive_bound COMMAND primeforge-adaptive-bound-tests)
    add_test(
        NAME primeforge.external_adapters
        COMMAND primeforge-external-adapter-tests
            $<TARGET_FILE:primeforge-adapter-fixture>
            ${CMAKE_CURRENT_BINARY_DIR}/external-adapter-test-work
    )
    add_test(
        NAME primeforge.congruence_cli
        COMMAND primeforge-congruence
            --base 2
            --constant 1
            --k-min 1
            --k-max 31
            --k-step 2
            --n-min 0
            --n-max 12
            --n-step 2
            --k-parity odd
            --prime 3
            --prime 5
            --prime 7
            --period-multiplier 2
    )
    add_test(
        NAME primeforge.work_units_cli
        COMMAND primeforge-work-units
            --begin 0
            --end 1000
            --span 137
            --output-dir ${CMAKE_CURRENT_BINARY_DIR}/work-unit-smoke
            --family-id family.test.v1
            --definition-sha256 09367f1d4242c68f3546995f3566c69fc5ff80cc6fcc8bd02c4368fcd905ce60
            --constraint n%3!=0
            --constraint k%2==1
            --proof-policy PROVE_IF_SURVIVES
            --seed 20260802
    )
    add_test(
        NAME primeforge.benchmark_smoke
        COMMAND primeforge-benchmark
            --output-dir ${CMAKE_CURRENT_BINARY_DIR}/benchmark-smoke
            --candidates ${CMAKE_CURRENT_SOURCE_DIR}/corpus/v1/cases.tsv
            --run-id ctest
            --commit ctest
            --candidate-set-sha256 2B61CB65EF67D14CD8DED6F19EC1285FA1F1AB1A1EDD33717C8EA8326AB9D79B
            --environment-metadata-file NOT_APPLICABLE
            --repetitions 7
            --seed 20260802
    )

    add_test(
        NAME primeforge.license_distribution
        COMMAND ${CMAKE_COMMAND}
            -DPRIMEFORGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/licenses/DISTRIBUTION_MANIFEST.tsv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDistributionLicenses.cmake
    )
    add_test(
        NAME primeforge.license_missing_negative
        COMMAND ${CMAKE_COMMAND}
            -DPRIMEFORGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/data/distribution_manifest_missing_license.tsv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDistributionLicenses.cmake
    )
    set_tests_properties(primeforge.license_missing_negative PROPERTIES WILL_FAIL TRUE)
endif()

add_custom_target(
    primeforge-distribution-check
    COMMAND ${CMAKE_COMMAND}
        -DPRIMEFORGE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/licenses/DISTRIBUTION_MANIFEST.tsv
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDistributionLicenses.cmake
    COMMENT "Verifying distribution license manifest"
    VERBATIM
)
