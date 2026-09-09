#include "oot3d_ui/ui_native_actions.h"

#include <type_traits>

namespace oot3d::ui {

namespace {

constexpr std::array<UiNativeActionContract, kUiNativeActionContractCount>
    kOot3dNativeUiActionContracts{{
        {UiNativeActionKind::ChangeHudVisibility, 0x0034BE04, 40,
         UiSubsystem::GameplayHud, UiSemanticMechanic::HudVisibility,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::TryEquipCategorySlot, 0x002E9B4C, 528,
         UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::ContinueSaveFlow, 0x004392C8, 6420,
         UiSubsystem::SystemMenu, UiSemanticMechanic::SystemSave,
         UiNativeActionInvocation::NativeFrameContinuation, UiNativeActionResultKind::None, true},
        {UiNativeActionKind::ReplaceInventoryItem, 0x00316CEC, 132,
         UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::Boolean, true},
        {UiNativeActionKind::ChangeEquipment, 0x0033187C, 52,
         UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::SwapPauseInventorySlots, 0x0033C1B8, 80,
         UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::RefreshPauseInventory, 0x0033C25C, 1196,
         UiSubsystem::Items, UiSemanticMechanic::InventoryItems,
         UiNativeActionInvocation::NativeMaintenance, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::ChangeUpgrade, 0x0033C730, 40,
         UiSubsystem::Equipment, UiSemanticMechanic::InventoryUpgrades,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::ChangeAmmo, 0x00355830, 540,
         UiSubsystem::Items, UiSemanticMechanic::InventoryAmmo,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, false},
        {UiNativeActionKind::DeleteEquipment, 0x0035D190, 180,
         UiSubsystem::Equipment, UiSemanticMechanic::InventoryEquipment,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::ItemId, true},
        {UiNativeActionKind::UpdateBottleItem, 0x004C1044, 72,
         UiSubsystem::Items, UiSemanticMechanic::InventoryBottles,
         UiNativeActionInvocation::BackendRequest, UiNativeActionResultKind::None, true},
    }};

constexpr std::uint32_t ContractBytes() noexcept {
    std::uint32_t bytes = 0;
    for (const auto& contract : kOot3dNativeUiActionContracts) {
        bytes += contract.guest_size;
    }
    return bytes;
}

constexpr std::size_t InvocationCount(UiNativeActionInvocation invocation) noexcept {
    std::size_t count = 0;
    for (const auto& contract : kOot3dNativeUiActionContracts) {
        count += contract.invocation == invocation;
    }
    return count;
}

constexpr std::size_t NativePlayStateCount() noexcept {
    std::size_t count = 0;
    for (const auto& contract : kOot3dNativeUiActionContracts) {
        count += contract.requires_native_play_state;
    }
    return count;
}

constexpr bool EntriesAndKindsAreUnique() noexcept {
    for (std::size_t left = 0; left < kOot3dNativeUiActionContracts.size(); ++left) {
        if (static_cast<std::size_t>(kOot3dNativeUiActionContracts[left].kind) != left) {
            return false;
        }
        for (std::size_t right = left + 1; right < kOot3dNativeUiActionContracts.size(); ++right) {
            if (kOot3dNativeUiActionContracts[left].guest_entry ==
                    kOot3dNativeUiActionContracts[right].guest_entry ||
                kOot3dNativeUiActionContracts[left].kind ==
                    kOot3dNativeUiActionContracts[right].kind) {
                return false;
            }
        }
    }
    return true;
}

template <typename Action>
constexpr UiNativeActionKind ActionKindFor() noexcept {
    if constexpr (std::is_same_v<Action, ChangeHudVisibilityRequest>) {
        return UiNativeActionKind::ChangeHudVisibility;
    } else if constexpr (std::is_same_v<Action, TryEquipCategorySlotRequest>) {
        return UiNativeActionKind::TryEquipCategorySlot;
    } else if constexpr (std::is_same_v<Action, ContinueSaveFlowOperation>) {
        return UiNativeActionKind::ContinueSaveFlow;
    } else if constexpr (std::is_same_v<Action, ReplaceInventoryItemRequest>) {
        return UiNativeActionKind::ReplaceInventoryItem;
    } else if constexpr (std::is_same_v<Action, ChangeEquipmentRequest>) {
        return UiNativeActionKind::ChangeEquipment;
    } else if constexpr (std::is_same_v<Action, SwapPauseInventorySlotsRequest>) {
        return UiNativeActionKind::SwapPauseInventorySlots;
    } else if constexpr (std::is_same_v<Action, RefreshPauseInventoryOperation>) {
        return UiNativeActionKind::RefreshPauseInventory;
    } else if constexpr (std::is_same_v<Action, ChangeUpgradeRequest>) {
        return UiNativeActionKind::ChangeUpgrade;
    } else if constexpr (std::is_same_v<Action, ChangeAmmoRequest>) {
        return UiNativeActionKind::ChangeAmmo;
    } else if constexpr (std::is_same_v<Action, DeleteEquipmentRequest>) {
        return UiNativeActionKind::DeleteEquipment;
    } else {
        static_assert(std::is_same_v<Action, UpdateBottleItemRequest>);
        return UiNativeActionKind::UpdateBottleItem;
    }
}

template <typename Variant>
UiNativeActionKind ActionKindOfVariant(const Variant& operation) noexcept {
    return std::visit(
        [](const auto& value) {
            return ActionKindFor<std::decay_t<decltype(value)>>();
        },
        operation);
}

static_assert(std::variant_size_v<UiBackendNativeActionRequest> ==
              kUiBackendRequestableActionCount);
static_assert(std::variant_size_v<UiNativeInternalOperation> ==
              kUiNativeInternalOperationCount);
static_assert(std::variant_size_v<UiNativeActionOperation> ==
              kUiNativeActionContractCount);
static_assert(ContractBytes() == 9280, "native UI action byte coverage drifted");
static_assert(InvocationCount(UiNativeActionInvocation::BackendRequest) ==
              kUiBackendRequestableActionCount);
static_assert(InvocationCount(UiNativeActionInvocation::NativeFrameContinuation) == 1);
static_assert(InvocationCount(UiNativeActionInvocation::NativeMaintenance) == 1);
static_assert(NativePlayStateCount() == 4);
static_assert(EntriesAndKindsAreUnique());

} // namespace

const std::array<UiNativeActionContract, kUiNativeActionContractCount>&
Oot3dNativeUiActionContracts() noexcept {
    return kOot3dNativeUiActionContracts;
}

UiNativeActionKind UiNativeActionKindOf(
    const UiBackendNativeActionRequest& request) noexcept {
    return ActionKindOfVariant(request);
}

UiNativeActionKind UiNativeActionKindOf(
    const UiNativeInternalOperation& operation) noexcept {
    return ActionKindOfVariant(operation);
}

UiNativeActionKind UiNativeActionKindOf(
    const UiNativeActionOperation& operation) noexcept {
    return ActionKindOfVariant(operation);
}

const UiNativeActionContract* FindOot3dNativeUiActionContract(
    UiNativeActionKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= kOot3dNativeUiActionContracts.size()) {
        return nullptr;
    }
    return &kOot3dNativeUiActionContracts[index];
}

bool IsUiNativeActionRequestableByBackend(UiNativeActionKind kind) noexcept {
    const UiNativeActionContract* contract = FindOot3dNativeUiActionContract(kind);
    return contract != nullptr &&
           contract->invocation == UiNativeActionInvocation::BackendRequest;
}

const char* UiNativeActionKindName(UiNativeActionKind kind) noexcept {
    switch (kind) {
    case UiNativeActionKind::ChangeHudVisibility: return "change_hud_visibility";
    case UiNativeActionKind::TryEquipCategorySlot: return "try_equip_category_slot";
    case UiNativeActionKind::ContinueSaveFlow: return "continue_save_flow";
    case UiNativeActionKind::ReplaceInventoryItem: return "replace_inventory_item";
    case UiNativeActionKind::ChangeEquipment: return "change_equipment";
    case UiNativeActionKind::SwapPauseInventorySlots: return "swap_pause_inventory_slots";
    case UiNativeActionKind::RefreshPauseInventory: return "refresh_pause_inventory";
    case UiNativeActionKind::ChangeUpgrade: return "change_upgrade";
    case UiNativeActionKind::ChangeAmmo: return "change_ammo";
    case UiNativeActionKind::DeleteEquipment: return "delete_equipment";
    case UiNativeActionKind::UpdateBottleItem: return "update_bottle_item";
    case UiNativeActionKind::Count: return "invalid";
    }
    return "invalid";
}

const char* UiNativeActionInvocationName(UiNativeActionInvocation invocation) noexcept {
    switch (invocation) {
    case UiNativeActionInvocation::BackendRequest: return "backend_request";
    case UiNativeActionInvocation::NativeFrameContinuation: return "native_frame_continuation";
    case UiNativeActionInvocation::NativeMaintenance: return "native_maintenance";
    }
    return "invalid";
}

const char* UiNativeActionResultKindName(UiNativeActionResultKind result) noexcept {
    switch (result) {
    case UiNativeActionResultKind::None: return "none";
    case UiNativeActionResultKind::Boolean: return "boolean";
    case UiNativeActionResultKind::ItemId: return "item_id";
    }
    return "invalid";
}

} // namespace oot3d::ui
