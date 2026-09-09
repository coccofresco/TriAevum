#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_inventory_grid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace oot3d::ui {

// These are semantic requests, not guest calls.  A future runtime adapter may
// inject native context and dispatch them only after the corresponding UI
// backend is enabled.  The current native profile emits and dispatches none.
enum class UiNativeActionKind : std::uint8_t {
    ChangeHudVisibility,
    TryEquipCategorySlot,
    ContinueSaveFlow,
    ReplaceInventoryItem,
    ChangeEquipment,
    SwapPauseInventorySlots,
    RefreshPauseInventory,
    ChangeUpgrade,
    ChangeAmmo,
    DeleteEquipment,
    UpdateBottleItem,
    Count,
};

enum class UiNativeActionInvocation : std::uint8_t {
    BackendRequest,
    NativeFrameContinuation,
    NativeMaintenance,
};

enum class UiNativeActionResultKind : std::uint8_t {
    None,
    Boolean,
    ItemId,
};

struct ChangeHudVisibilityRequest {
    HudVisibilityMode mode{};
};

struct TryEquipCategorySlotRequest {
    EquipmentType category{};
    std::int32_t slot = 0;
};

struct ContinueSaveFlowOperation {};

struct ReplaceInventoryItemRequest {
    ItemId old_item_id{};
    ItemId new_item_id{};
};

struct ChangeEquipmentRequest {
    EquipmentType equipment_type{};
    std::int32_t value = 0;
};

struct SwapPauseInventorySlotsRequest {
    PauseItemGridPosition position_a{};
    PauseItemGridPosition position_b{};
};

struct RefreshPauseInventoryOperation {
    bool force = false;
};

struct ChangeUpgradeRequest {
    UpgradeType upgrade_type{};
    std::int32_t value = 0;
};

struct ChangeAmmoRequest {
    ItemId item_id{};
    std::int32_t delta = 0;
};

struct DeleteEquipmentRequest {
    EquipmentType equipment_type{};
};

struct UpdateBottleItemRequest {
    ItemId item_id{};
    std::uint8_t button_index = 0;
};

using UiBackendNativeActionRequest = std::variant<
    ChangeHudVisibilityRequest,
    TryEquipCategorySlotRequest,
    ReplaceInventoryItemRequest,
    ChangeEquipmentRequest,
    SwapPauseInventorySlotsRequest,
    ChangeUpgradeRequest,
    ChangeAmmoRequest,
    DeleteEquipmentRequest,
    UpdateBottleItemRequest>;

using UiNativeInternalOperation = std::variant<
    ContinueSaveFlowOperation,
    RefreshPauseInventoryOperation>;

using UiNativeActionOperation = std::variant<
    ChangeHudVisibilityRequest,
    TryEquipCategorySlotRequest,
    ContinueSaveFlowOperation,
    ReplaceInventoryItemRequest,
    ChangeEquipmentRequest,
    SwapPauseInventorySlotsRequest,
    RefreshPauseInventoryOperation,
    ChangeUpgradeRequest,
    ChangeAmmoRequest,
    DeleteEquipmentRequest,
    UpdateBottleItemRequest>;

struct UiNativeActionContract {
    UiNativeActionKind kind = UiNativeActionKind::ChangeHudVisibility;
    std::uint32_t guest_entry = 0;
    std::uint32_t guest_size = 0;
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    UiSemanticMechanic mechanic = UiSemanticMechanic::HudVisibility;
    UiNativeActionInvocation invocation = UiNativeActionInvocation::BackendRequest;
    UiNativeActionResultKind result = UiNativeActionResultKind::None;
    bool requires_native_play_state = false;
};

inline constexpr std::size_t kUiNativeActionContractCount = 11;
inline constexpr std::size_t kUiBackendRequestableActionCount = 9;
inline constexpr std::size_t kUiNativeInternalOperationCount = 2;

const std::array<UiNativeActionContract, kUiNativeActionContractCount>&
Oot3dNativeUiActionContracts() noexcept;
UiNativeActionKind UiNativeActionKindOf(
    const UiBackendNativeActionRequest& request) noexcept;
UiNativeActionKind UiNativeActionKindOf(
    const UiNativeInternalOperation& operation) noexcept;
UiNativeActionKind UiNativeActionKindOf(
    const UiNativeActionOperation& operation) noexcept;
const UiNativeActionContract* FindOot3dNativeUiActionContract(
    UiNativeActionKind kind) noexcept;
bool IsUiNativeActionRequestableByBackend(UiNativeActionKind kind) noexcept;
const char* UiNativeActionKindName(UiNativeActionKind kind) noexcept;
const char* UiNativeActionInvocationName(UiNativeActionInvocation invocation) noexcept;
const char* UiNativeActionResultKindName(UiNativeActionResultKind result) noexcept;

} // namespace oot3d::ui
