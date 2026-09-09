#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_inventory_grid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace oot3d::ui {

// These contracts describe retained native reads. They do not call guest code;
// a future runtime adapter must validate requests and invoke the exact native
// query only after a non-native backend is explicitly enabled.
enum class UiNativeQueryKind : std::uint8_t {
    FindPauseItemGridPosition,
    HasEmptyBottle,
    Count,
};

enum class UiNativeQueryResultKind : std::uint8_t {
    PauseItemGridPosition,
    Boolean,
};

struct FindPauseItemGridPositionQuery {
    ItemId item_id{};
};

struct HasEmptyBottleQuery {};

using UiNativeQueryRequest = std::variant<
    FindPauseItemGridPositionQuery,
    HasEmptyBottleQuery>;

struct FindPauseItemGridPositionResult {
    PauseItemGridPosition position{};
};

struct HasEmptyBottleResult {
    bool has_empty_bottle = false;
};

using UiNativeQueryResult = std::variant<
    FindPauseItemGridPositionResult,
    HasEmptyBottleResult>;

struct UiNativeQueryContract {
    UiNativeQueryKind kind = UiNativeQueryKind::FindPauseItemGridPosition;
    std::uint32_t guest_entry = 0;
    std::uint32_t guest_size = 0;
    UiSubsystem subsystem = UiSubsystem::Items;
    UiSemanticMechanic mechanic = UiSemanticMechanic::InventoryLookup;
    UiNativeQueryResultKind result = UiNativeQueryResultKind::PauseItemGridPosition;
    bool requires_mapped_item_id = false;
    bool has_n64_semantic_reference = false;
};

inline constexpr std::size_t kUiNativeQueryContractCount = 2;

const std::array<UiNativeQueryContract, kUiNativeQueryContractCount>&
Oot3dNativeUiQueryContracts() noexcept;
UiNativeQueryKind UiNativeQueryKindOf(const UiNativeQueryRequest& request) noexcept;
const UiNativeQueryContract* FindOot3dNativeUiQueryContract(
    UiNativeQueryKind kind) noexcept;
bool IsUiNativeQueryRequestValid(const UiNativeQueryRequest& request) noexcept;
const char* UiNativeQueryKindName(UiNativeQueryKind kind) noexcept;
const char* UiNativeQueryResultKindName(UiNativeQueryResultKind result) noexcept;

} // namespace oot3d::ui
