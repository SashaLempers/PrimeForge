# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(
    PrimeForge
    VERSION 0.1.0
    DESCRIPTION "Auditable prime-search, proof, and verification research platform"
    LANGUAGES CXX
)

include(CTest)

option(PRIMEFORGE_ENABLE_CUDA "Build the local CUDA validation target" OFF)
set(PRIMEFORGE_CUDA_ROOT "" CACHE PATH "Pinned local CUDA Toolkit root")

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
    src/cpu/cpu_topology.cpp
    src/engine/external_adapter.cpp
    src/family/family.cpp
    src/math/big_integer.cpp
    src/math/mul128.cpp
    src/mvp/search_config.cpp
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

add_library(primeforge_mvp STATIC
    src/mvp/campaign_verifier.cpp
    src/mvp/search_pipeline.cpp
)
add_library(PrimeForge::mvp ALIAS primeforge_mvp)
target_include_directories(primeforge_mvp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(primeforge_mvp PUBLIC PrimeForge::core PrimeForge::adaptive)
target_compile_features(primeforge_mvp PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_mvp)

add_library(primeforge_runtime STATIC
    src/runtime/benchmark_logger.cpp
    src/runtime/benchmark_watchdog.cpp
    src/runtime/checkpoint_manager.cpp
    src/runtime/hardware_monitor.cpp
)
add_library(PrimeForge::runtime ALIAS primeforge_runtime)
target_include_directories(primeforge_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(primeforge_runtime PUBLIC PrimeForge::core)
target_compile_features(primeforge_runtime PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_runtime)
target_link_libraries(primeforge_mvp PUBLIC PrimeForge::runtime)

add_library(primeforge_pipeline STATIC src/pipeline/modular_pipeline.cpp)
add_library(PrimeForge::pipeline ALIAS primeforge_pipeline)
target_include_directories(primeforge_pipeline
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(primeforge_pipeline PUBLIC PrimeForge::runtime PrimeForge::core)
target_compile_features(primeforge_pipeline PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_pipeline)

if(WIN32)
    target_compile_definitions(primeforge_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(primeforge_core PRIVATE user32)
    target_compile_definitions(primeforge_runtime PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(primeforge_runtime PRIVATE PowrProf)
endif()

add_executable(primeforge-selftest src/selftest/main.cpp)
target_link_libraries(primeforge-selftest PRIVATE PrimeForge::core)
target_compile_features(primeforge-selftest PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-selftest)

add_executable(primeforge src/cli/main.cpp)
target_link_libraries(primeforge PRIVATE PrimeForge::mvp)
target_compile_features(primeforge PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge)

add_executable(primeforge-mvp-tests tests/mvp_test.cpp)
target_link_libraries(primeforge-mvp-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-mvp-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-mvp-tests)

add_executable(primeforge-mvp-pipeline-tests tests/mvp_pipeline_test.cpp)
target_link_libraries(primeforge-mvp-pipeline-tests PRIVATE PrimeForge::mvp)
target_compile_features(primeforge-mvp-pipeline-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-mvp-pipeline-tests)

add_executable(primeforge-tests tests/test_main.cpp)
target_link_libraries(primeforge-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-tests)

add_executable(primeforge-modular-pipeline-tests tests/modular_pipeline_test.cpp)
target_link_libraries(primeforge-modular-pipeline-tests PRIVATE PrimeForge::pipeline)
target_compile_features(primeforge-modular-pipeline-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-modular-pipeline-tests)

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

add_executable(hardware_monitor src/runtime/hardware_monitor_main.cpp)
target_link_libraries(hardware_monitor PRIVATE PrimeForge::runtime)
target_compile_features(hardware_monitor PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(hardware_monitor)

add_executable(benchmark_logger src/runtime/benchmark_logger_main.cpp)
target_link_libraries(benchmark_logger PRIVATE PrimeForge::runtime)
target_compile_features(benchmark_logger PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(benchmark_logger)

add_executable(checkpoint_manager src/runtime/checkpoint_manager_main.cpp)
target_link_libraries(checkpoint_manager PRIVATE PrimeForge::runtime)
target_compile_features(checkpoint_manager PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(checkpoint_manager)

add_executable(benchmark_watchdog src/runtime/benchmark_watchdog_main.cpp)
target_link_libraries(benchmark_watchdog PRIVATE PrimeForge::runtime)
target_compile_features(benchmark_watchdog PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(benchmark_watchdog)

add_executable(primeforge-runtime-tests tests/runtime_test.cpp)
target_link_libraries(primeforge-runtime-tests PRIVATE PrimeForge::runtime)
target_compile_features(primeforge-runtime-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-runtime-tests)

add_executable(primeforge-watchdog-fixture tests/watchdog_fixture.cpp)
target_compile_features(primeforge-watchdog-fixture PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-watchdog-fixture)

add_executable(primeforge-watchdog-process-tests tests/watchdog_process_test.cpp)
target_compile_features(primeforge-watchdog-process-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-watchdog-process-tests)

add_executable(primeforge-cpu-topology src/cpu/main.cpp)
target_link_libraries(primeforge-cpu-topology PRIVATE PrimeForge::core)
target_compile_features(primeforge-cpu-topology PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-cpu-topology)

add_executable(primeforge-cpu-topology-tests tests/cpu_topology_test.cpp)
target_link_libraries(primeforge-cpu-topology-tests PRIVATE PrimeForge::core)
target_compile_features(primeforge-cpu-topology-tests PRIVATE cxx_std_23)
primeforge_enable_strict_warnings(primeforge-cpu-topology-tests)

if(PRIMEFORGE_ENABLE_CUDA)
    if(PRIMEFORGE_CUDA_ROOT STREQUAL "")
        message(FATAL_ERROR "PRIMEFORGE_CUDA_ROOT is required when CUDA validation is enabled")
    endif()
    find_program(
        primeforge_nvcc
        NAMES nvcc nvcc.exe
        HINTS "${PRIMEFORGE_CUDA_ROOT}/bin"
        NO_DEFAULT_PATH
        REQUIRED
    )
    set(CMAKE_CUDA_COMPILER "${primeforge_nvcc}" CACHE FILEPATH "Pinned nvcc" FORCE)
    enable_language(CUDA)
    set(CUDAToolkit_ROOT "${PRIMEFORGE_CUDA_ROOT}")
    find_package(CUDAToolkit 13.3 REQUIRED)
    if(CUDAToolkit_VERSION VERSION_LESS 13.3 OR
       NOT CUDAToolkit_VERSION VERSION_LESS 13.4)
        message(FATAL_ERROR "PrimeForge PIVOT-04 requires CUDA Toolkit 13.3.x")
    endif()

    function(primeforge_configure_cuda_target target)
        target_compile_features(${target} PRIVATE cuda_std_20)
        target_compile_options(
            ${target}
            PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--Werror=all-warnings>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/W4>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/WX>
            # NVCC 13.3 emits C4211 in its generated registration stub. Keep
            # the suppression inside this CUDA target; every other warning
            # and all CUDA frontend warnings remain errors.
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/wd4211>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/permissive->
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/EHsc>
            $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>
        )
        set_target_properties(
            ${target}
            PROPERTIES
            CUDA_ARCHITECTURES 120
            CUDA_EXTENSIONS OFF
            CUDA_SEPARABLE_COMPILATION OFF
        )
    endfunction()

    add_executable(primeforge-cuda-validation src/cuda/validation.cu)
    target_link_libraries(primeforge-cuda-validation PRIVATE CUDA::cudart)
    primeforge_configure_cuda_target(primeforge-cuda-validation)

    add_library(primeforge_cuda STATIC src/cuda/modular_batch.cu)
    add_library(PrimeForge::cuda ALIAS primeforge_cuda)
    target_include_directories(
        primeforge_cuda
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(primeforge_cuda PUBLIC CUDA::cudart)
    primeforge_configure_cuda_target(primeforge_cuda)

    add_executable(primeforge-cuda-modular-tests tests/cuda_modular_batch_test.cpp)
    target_link_libraries(
        primeforge-cuda-modular-tests
        PRIVATE PrimeForge::cuda PrimeForge::core
    )
    target_compile_features(primeforge-cuda-modular-tests PRIVATE cxx_std_23)
    primeforge_enable_strict_warnings(primeforge-cuda-modular-tests)

    add_executable(primeforge-cuda-pipeline-tests tests/cuda_pipeline_test.cpp)
    target_link_libraries(
        primeforge-cuda-pipeline-tests
        PRIVATE PrimeForge::cuda PrimeForge::pipeline PrimeForge::core
    )
    target_compile_features(primeforge-cuda-pipeline-tests PRIVATE cxx_std_23)
    primeforge_enable_strict_warnings(primeforge-cuda-pipeline-tests)
endif()

if(BUILD_TESTING)
    add_test(NAME primeforge.unit COMMAND primeforge-tests)
    add_test(NAME primeforge.modular_pipeline COMMAND primeforge-modular-pipeline-tests)
    add_test(NAME primeforge.selftest COMMAND primeforge-selftest)
    add_test(NAME primeforge.mvp_planning COMMAND primeforge-mvp-tests)
    add_test(
        NAME primeforge.mvp_pipeline
        COMMAND primeforge-mvp-pipeline-tests
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/mvp/search.yaml
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/mvp/known_proth_small_primes.tsv
    )
    add_test(NAME primeforge.mvp_cli_selftest COMMAND primeforge selftest)
    add_test(
        NAME primeforge.mvp_cli_inspect
        COMMAND primeforge inspect --config ${CMAKE_CURRENT_SOURCE_DIR}/examples/mvp/search.yaml
    )
    add_test(
        NAME primeforge.mvp_cli_hash_mismatch
        COMMAND primeforge inspect
            --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/search_hash_mismatch.yaml
    )
    set_tests_properties(
        primeforge.mvp_cli_hash_mismatch
        PROPERTIES WILL_FAIL TRUE WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
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
    add_test(NAME primeforge.runtime COMMAND primeforge-runtime-tests)
    add_test(NAME primeforge.cpu_topology COMMAND primeforge-cpu-topology-tests)
    add_test(NAME primeforge.cpu_topology_cli COMMAND primeforge-cpu-topology)
    if(PRIMEFORGE_ENABLE_CUDA)
        add_test(NAME primeforge.cuda_validation COMMAND primeforge-cuda-validation)
        add_test(NAME primeforge.cuda_modular_batch COMMAND primeforge-cuda-modular-tests)
        add_test(NAME primeforge.cuda_pipeline COMMAND primeforge-cuda-pipeline-tests)
    endif()
    add_test(
        NAME primeforge.watchdog_process
        COMMAND primeforge-watchdog-process-tests
            $<TARGET_FILE:benchmark_watchdog>
            $<TARGET_FILE:primeforge-watchdog-fixture>
            ${CMAKE_CURRENT_BINARY_DIR}/watchdog-process-test
    )
    add_test(NAME primeforge.hardware_monitor_cli COMMAND hardware_monitor --once)
    add_test(
        NAME primeforge.benchmark_logger_cli
        COMMAND benchmark_logger
            --log ${CMAKE_CURRENT_BINARY_DIR}/runtime-smoke/events.jsonl
            --campaign ctest-runtime
            --event smoke
            --payload-json "{}"
    )
    add_test(
        NAME primeforge.checkpoint_write_cli
        COMMAND checkpoint_manager write
            --path ${CMAKE_CURRENT_BINARY_DIR}/runtime-smoke/checkpoint.json
            --campaign ctest-runtime
            --sequence 7
            --progress 1000
            --payload resumable
    )
    add_test(
        NAME primeforge.checkpoint_read_cli
        COMMAND checkpoint_manager read
            --path ${CMAKE_CURRENT_BINARY_DIR}/runtime-smoke/checkpoint.json
    )
    set_tests_properties(primeforge.checkpoint_read_cli PROPERTIES DEPENDS primeforge.checkpoint_write_cli)
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

    if(WIN32)
        add_test(
            NAME primeforge.hardware_profile
            COMMAND powershell.exe
                -NoProfile
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_hardware_profile.ps1
                -Collector ${CMAKE_CURRENT_SOURCE_DIR}/scripts/collect_hardware_profile.ps1
                -SelfTestPath $<TARGET_FILE:primeforge-selftest>
                -WorkingDirectory ${CMAKE_CURRENT_BINARY_DIR}/hardware-profile-test
        )
    endif()
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

if(WIN32)
    add_custom_target(
        primeforge-mvp-package
        COMMAND powershell.exe
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_mvp.ps1
            -Version 0.1.0-mvp
            -Executable $<TARGET_FILE:primeforge>
        DEPENDS primeforge primeforge-distribution-check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building and verifying the closed PrimeForge MVP package"
        VERBATIM
    )
endif()
