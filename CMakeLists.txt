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
)
add_library(PrimeForge::core ALIAS primeforge_core)

target_include_directories(primeforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(primeforge_core PUBLIC cxx_std_23)
primeforge_enable_strict_warnings(primeforge_core)

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

if(BUILD_TESTING)
    add_test(NAME primeforge.unit COMMAND primeforge-tests)
    add_test(NAME primeforge.selftest COMMAND primeforge-selftest)
endif()
