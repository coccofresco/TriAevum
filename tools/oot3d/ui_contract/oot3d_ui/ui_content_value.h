#pragma once

#include <cstdint>

namespace oot3d::ui {

// Derived UI content needs to distinguish a missing source read from a value
// that does not exist for the selected native slot. SemanticValue remains the
// exact guest-read carrier; this type is only for read-only host projections.
enum class UiContentValueState : std::uint8_t {
    Unknown,
    NotApplicable,
    Known,
};

template <typename T>
struct UiContentValue {
    T value{};
    UiContentValueState state = UiContentValueState::Unknown;

    constexpr bool IsKnown() const noexcept {
        return state == UiContentValueState::Known;
    }

    constexpr bool IsApplicable() const noexcept {
        return state != UiContentValueState::NotApplicable;
    }
};

template <typename T>
constexpr UiContentValue<T> KnownUiContentValue(T value) noexcept {
    return {value, UiContentValueState::Known};
}

template <typename T>
constexpr UiContentValue<T> NotApplicableUiContentValue() noexcept {
    return {{}, UiContentValueState::NotApplicable};
}

} // namespace oot3d::ui
