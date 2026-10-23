#pragma once

#include <compare>
#include <cstdint>

namespace Fast::Renderer3ds {

enum class PicaTemporalMatchReason : uint8_t {
    Exact, MissingIdentity, Ambiguous, Unpublished, InvalidSample, Reset,
    MissingPrevious, IncompatibleProgramOrLayout, ChangedInputs, UniformSize,
    DuplicateSubmission, Count
};

// Opaque native instance evidence. No geometry hash, draw ordinal, command-list
// destination or transform participates. The producer owns lifetime changes.
// Part/Slot distinguish persistent draws inside one instance; duplicates in a
// complete presented view are ambiguous, including the first duplicate.
struct PicaTemporalIdentity {
    uint64_t Owner = 0;
    uint64_t Instance = 0;
    uint64_t Part = 0;
    uint64_t Slot = 0;
    uint64_t Lifetime = 0;

    [[nodiscard]] bool Available() const noexcept {
        return Instance != 0 && Part != 0 && Lifetime != 0;
    }
    auto operator<=>(const PicaTemporalIdentity&) const = default;
};

} // namespace Fast::Renderer3ds
