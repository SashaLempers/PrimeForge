// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <iostream>
#include <string_view>
#include <thread>

int main(const int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string_view command{argv[1]};
    if (argc == 2 && command == "SLEEP") {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        std::cout << "FIXTURE:PRP\n";
        return 0;
    }
    for (int index = 1; index < argc; ++index) std::cout << argv[index] << '\n';
    return 0;
}
