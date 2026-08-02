// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace primeforge {

using Sha256Digest = std::array<std::byte, 32>;

class Sha256Provider {
public:
    virtual ~Sha256Provider() = default;

    [[nodiscard]] virtual Sha256Digest digest(std::span<const std::byte> bytes) const = 0;
};

class PortableSha256Provider final : public Sha256Provider {
public:
    [[nodiscard]] Sha256Digest digest(std::span<const std::byte> bytes) const override;
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> bytes);

[[nodiscard]] std::string sha256_to_hex(const Sha256Digest& digest);
[[nodiscard]] std::optional<Sha256Digest> sha256_from_hex(std::string_view hex) noexcept;

} // namespace primeforge
