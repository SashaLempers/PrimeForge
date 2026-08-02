# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(
    PrimeForge
    VERSION 0.1.0
    DESCRIPTION "Auditable prime-search, proof, and verification research platform"
    LANGUAGES CXX
)

include(CTest)

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

add_library(primeforge_core STATIC
    src/core/sha256.cpp
    src/core/status.cpp
    src/core/system_info.cpp
    src/math/mul128.cpp
    src/sieve/sieve.cpp
    src/work/work_unit.cpp
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
