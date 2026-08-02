#include "primeforge/core/system_info.hpp"

#include <iostream>

int main() {
    const auto info = primeforge::collect_system_info();
    std::cout << "PrimeForge self-test\n";
    std::cout << primeforge::format_system_info(info);

    // GCC and Clang report 202100L for their C++23 modes on supported CI
    // versions, while current MSVC reports 202400L. CMake's cxx_std_23 feature
    // remains the authoritative compile-mode request.
    if (info.compiler.cplusplus < 202100L) {
        std::cerr << "ERROR: the active language mode is older than C++23.\n";
        return 1;
    }
    if (info.compiler.name == "UNKNOWN" || info.operating_system.name == "UNKNOWN") {
        std::cerr << "ERROR: compiler or operating system could not be identified.\n";
        return 1;
    }
    if (info.electrical_power_watts != "UNKNOWN") {
        std::cerr << "ERROR: stage 1 must not infer an electrical power measurement.\n";
        return 1;
    }

    std::cout << "selftest.status=PASS\n";
    return 0;
}
