#include "oot3d_ui/ui_native_queries.h"

#include <type_traits>

namespace oot3d::ui {

namespace {

constexpr std::array<UiNativeQueryContract, kUiNativeQueryContractCount>
    kOot3dNativeUiQueryContracts{{
        {UiNativeQueryKind::FindPauseItemGridPosition, 0x0033C20C, 72,
         UiSubsystem::Items, UiSemanticMechanic::InventoryLookup,
         UiNativeQueryResultKind::PauseItemGridPosition, true, false},
        {UiNativeQueryKind::HasEmptyBottle, 0x00377A04, 72,
         UiSubsystem::Items, UiSemanticMechanic::InventoryLookup,
         UiNativeQueryResultKind::Boolean, false, true},
    }};

constexpr std::uint32_t ContractBytes() noexcept {
    std::uint32_t bytes = 0;
    for (const auto& contract : kOot3dNativeUiQueryContracts) {
        bytes += contract.guest_size;
    }
    return bytes;
}

constexpr bool EntriesAndKindsAreUnique() noexcept {
    for (std::size_t left = 0; left < kOot3dNativeUiQueryContracts.size(); ++left) {
        if (static_cast<std::size_t>(kOot3dNativeUiQueryContracts[left].kind) != left) {
            return false;
        }
        for (std::size_t right = left + 1;
             right < kOot3dNativeUiQueryContracts.size(); ++right) {
            if (kOot3dNativeUiQueryContracts[left].guest_entry ==
                    kOot3dNativeUiQueryContracts[right].guest_entry ||
                kOot3dNativeUiQueryContracts[left].kind ==
                    kOot3dNativeUiQueryContracts[right].kind) {
                return false;
            }
        }
    }
    return true;
}

static_assert(std::variant_size_v<UiNativeQueryRequest> == kUiNativeQueryContractCount);
static_assert(std::variant_size_v<UiNativeQueryResult> == kUiNativeQueryContractCount);
static_assert(ContractBytes() == 144, "native UI query byte coverage drifted");
static_assert(EntriesAndKindsAreUnique());

} // namespace

const std::array<UiNativeQueryContract, kUiNativeQueryContractCount>&
Oot3dNativeUiQueryContracts() noexcept {
    return kOot3dNativeUiQueryContracts;
}

UiNativeQueryKind UiNativeQueryKindOf(const UiNativeQueryRequest& request) noexcept {
    return static_cast<UiNativeQueryKind>(request.index());
}

const UiNativeQueryContract* FindOot3dNativeUiQueryContract(
    UiNativeQueryKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= kOot3dNativeUiQueryContracts.size()) {
        return nullptr;
    }
    return &kOot3dNativeUiQueryContracts[index];
}

bool IsUiNativeQueryRequestValid(const UiNativeQueryRequest& request) noexcept {
    return std::visit(
        [](const auto& query) {
            using Query = std::decay_t<decltype(query)>;
            if constexpr (std::is_same_v<Query, FindPauseItemGridPositionQuery>) {
                return IsOot3dItemSlotMapped(query.item_id);
            }
            return true;
        },
        request);
}

const char* UiNativeQueryKindName(UiNativeQueryKind kind) noexcept {
    switch (kind) {
    case UiNativeQueryKind::FindPauseItemGridPosition:
        return "find_pause_item_grid_position";
    case UiNativeQueryKind::HasEmptyBottle:
        return "has_empty_bottle";
    case UiNativeQueryKind::Count:
        return "invalid";
    }
    return "invalid";
}

const char* UiNativeQueryResultKindName(UiNativeQueryResultKind result) noexcept {
    switch (result) {
    case UiNativeQueryResultKind::PauseItemGridPosition:
        return "pause_item_grid_position";
    case UiNativeQueryResultKind::Boolean:
        return "boolean";
    }
    return "invalid";
}

} // namespace oot3d::ui
