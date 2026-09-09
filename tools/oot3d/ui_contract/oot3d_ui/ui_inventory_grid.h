#pragma once

#include "oot3d_ui/ui_value_domains.h"

#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

inline constexpr std::size_t kPauseItemGridColumns = 6;
inline constexpr std::size_t kPauseItemGridRows = 4;
inline constexpr std::size_t kPauseItemGridPositionCount =
    kPauseItemGridColumns * kPauseItemGridRows;
inline constexpr std::size_t kOot3dItemMenuSlotCount = kPauseItemGridPositionCount;
inline constexpr std::uint8_t kInvalidPauseItemGridPosition = 0xFF;

// A grid position is not an InventorySlot. The native child/adult grids have
// 24 positions, and each position stores an InventorySlot value.
class PauseItemGridPosition {
public:
    constexpr PauseItemGridPosition() noexcept = default;

    static constexpr PauseItemGridPosition FromIndex(std::uint32_t index) noexcept {
        return index < kPauseItemGridPositionCount
                   ? PauseItemGridPosition(static_cast<std::uint8_t>(index))
                   : PauseItemGridPosition();
    }

    static constexpr PauseItemGridPosition FromNativeResult(std::int32_t result) noexcept {
        return result >= 0 ? FromIndex(static_cast<std::uint32_t>(result))
                           : PauseItemGridPosition();
    }

    constexpr bool IsValid() const noexcept {
        return index_ < kPauseItemGridPositionCount;
    }

    constexpr std::uint8_t Raw() const noexcept {
        return index_;
    }

    constexpr std::int32_t Column() const noexcept {
        return IsValid() ? index_ % kPauseItemGridColumns : -1;
    }

    constexpr std::int32_t Row() const noexcept {
        return IsValid() ? index_ / kPauseItemGridColumns : -1;
    }

    friend constexpr bool operator==(PauseItemGridPosition left,
                                     PauseItemGridPosition right) noexcept {
        return left.index_ == right.index_;
    }

    friend constexpr bool operator!=(PauseItemGridPosition left,
                                     PauseItemGridPosition right) noexcept {
        return !(left == right);
    }

private:
    explicit constexpr PauseItemGridPosition(std::uint8_t index) noexcept : index_(index) {}

    std::uint8_t index_ = kInvalidPauseItemGridPosition;
};

inline constexpr std::size_t kOot3dItemSlotMappingCount = 56;

constexpr bool IsOot3dItemSlotMapped(ItemId item_id) noexcept {
    return UiValueRaw(item_id) < kOot3dItemSlotMappingCount;
}

static_assert(sizeof(PauseItemGridPosition) == sizeof(std::uint8_t));
static_assert(PauseItemGridPosition::FromIndex(0).Row() == 0);
static_assert(PauseItemGridPosition::FromIndex(23).Column() == 5);
static_assert(!PauseItemGridPosition::FromIndex(24).IsValid());
static_assert(PauseItemGridPosition::FromNativeResult(0xFF).Raw() ==
              kInvalidPauseItemGridPosition);

} // namespace oot3d::ui
