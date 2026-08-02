// SPDX-License-Identifier: Apache-2.0

#include "primeforge/runtime/checkpoint_manager.hpp"

#include "primeforge/core/sha256.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string operation;
        std::string path;
        primeforge::runtime::CheckpointState state;
        state.progress_decimal = "0";
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "write" || argument == "read") { operation = argument; }
            else if (argument == "--path" && index + 1 < argc) { path = argv[++index]; }
            else if (argument == "--campaign" && index + 1 < argc) { state.campaign_id = argv[++index]; }
            else if (argument == "--payload" && index + 1 < argc) { state.opaque_payload = argv[++index]; }
            else if (argument == "--progress" && index + 1 < argc) { state.progress_decimal = argv[++index]; }
            else if (argument == "--sequence" && index + 1 < argc) { state.sequence = std::stoull(argv[++index]); }
            else { throw std::invalid_argument("usage: checkpoint_manager write|read --path PATH [--campaign ID --progress DECIMAL --sequence N --payload TEXT]"); }
        }
        if (path.empty() || (operation != "write" && operation != "read")) {
            throw std::invalid_argument("operation and checkpoint path are required");
        }
        primeforge::PortableSha256Provider sha256;
        primeforge::runtime::CheckpointManager manager(sha256);
        if (operation == "write") {
            manager.save(path, state);
            std::cout << "checkpoint_manager.status=SAVED\n";
        } else {
            const auto loaded = manager.load(path);
            std::cout << "checkpoint_manager.status=VALID\n";
            std::cout << "checkpoint_manager.campaign_id=" << loaded.campaign_id << '\n';
            std::cout << "checkpoint_manager.sequence=" << loaded.sequence << '\n';
            std::cout << "checkpoint_manager.progress_decimal=" << loaded.progress_decimal << '\n';
            std::cout << "checkpoint_manager.opaque_payload=" << loaded.opaque_payload << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "checkpoint_manager: " << error.what() << '\n';
        return 1;
    }
}
