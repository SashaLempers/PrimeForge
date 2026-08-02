// SPDX-License-Identifier: Apache-2.0

#include "primeforge/core/status.hpp"

namespace primeforge {

std::string_view to_string(const PrimalityStatus status) noexcept {
    switch (status) {
    case PrimalityStatus::untested:
        return "UNTESTED";
    case PrimalityStatus::composite:
        return "COMPOSITE";
    case PrimalityStatus::probable_prime:
        return "PROBABLE_PRIME";
    case PrimalityStatus::proven_prime:
        return "PROVEN_PRIME";
    }
    return "UNTESTED";
}

std::string_view to_string(const VerificationStatus status) noexcept {
    switch (status) {
    case VerificationStatus::unverified:
        return "UNVERIFIED";
    case VerificationStatus::self_verified:
        return "SELF_VERIFIED";
    case VerificationStatus::independently_verified:
        return "INDEPENDENTLY_VERIFIED";
    }
    return "UNVERIFIED";
}

std::string_view to_string(const NoveltyStatus status) noexcept {
    switch (status) {
    case NoveltyStatus::not_checked:
        return "NOT_CHECKED";
    case NoveltyStatus::check_in_progress:
        return "CHECK_IN_PROGRESS";
    case NoveltyStatus::due_diligence_complete:
        return "DUE_DILIGENCE_COMPLETE";
    case NoveltyStatus::previously_known:
        return "PREVIOUSLY_KNOWN";
    }
    return "NOT_CHECKED";
}

} // namespace primeforge
