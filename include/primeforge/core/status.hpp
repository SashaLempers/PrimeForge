#pragma once

#include <string_view>

namespace primeforge {

enum class PrimalityStatus {
    untested,
    composite,
    probable_prime,
    proven_prime,
};

enum class VerificationStatus {
    unverified,
    self_verified,
    independently_verified,
};

enum class NoveltyStatus {
    not_checked,
    check_in_progress,
    due_diligence_complete,
    previously_known,
};

struct CandidateStatus {
    PrimalityStatus primality{PrimalityStatus::untested};
    VerificationStatus verification{VerificationStatus::unverified};
    NoveltyStatus novelty{NoveltyStatus::not_checked};

    friend constexpr bool operator==(const CandidateStatus&, const CandidateStatus&) = default;
};

[[nodiscard]] std::string_view to_string(PrimalityStatus status) noexcept;
[[nodiscard]] std::string_view to_string(VerificationStatus status) noexcept;
[[nodiscard]] std::string_view to_string(NoveltyStatus status) noexcept;

} // namespace primeforge
