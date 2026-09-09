#include "oot3d_ui/ui_frontend_actions.h"

#include <type_traits>

namespace oot3d::ui {

namespace {

constexpr std::array<UiFrontendNativeOperationContract,
                     kUiFrontendNativeOperationContractCount>
    kOot3dFrontendNativeOperationContracts{{
        {UiFrontendNativeOperationKind::CopyFileSlot, 0x002EDA00, 248,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::BackendRequest, false},
        {UiFrontendNativeOperationKind::DeleteFileSlot, 0x00446FE4, 120,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::BackendRequest, false},
        {UiFrontendNativeOperationKind::LoadFileSlot, 0x0044737C, 696,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::BackendRequest, true},
        {UiFrontendNativeOperationKind::CancelFileSlotWrite, 0x002ED9D8, 36,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::NativeMaintenance, false},
        {UiFrontendNativeOperationKind::CancelFileSlotDelete, 0x00447148, 36,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::NativeMaintenance, false},
        {UiFrontendNativeOperationKind::RefreshFileSlotCatalog, 0x00447170, 492,
         UiSubsystem::FileSelect, UiSemanticMechanic::FileSelection,
         UiFrontendNativeInvocation::NativeMaintenance, false},
        {UiFrontendNativeOperationKind::ContinueNameEntrySaveCommit,
         0x0043E088, 368, UiSubsystem::NameEntry,
         UiSemanticMechanic::NameEntry,
         UiFrontendNativeInvocation::NativeFrameContinuation, false},
    }};

constexpr std::uint32_t ContractBytes() noexcept {
    std::uint32_t bytes = 0;
    for (const auto& contract : kOot3dFrontendNativeOperationContracts) {
        bytes += contract.guest_size;
    }
    return bytes;
}

constexpr std::size_t InvocationCount(
    UiFrontendNativeInvocation invocation) noexcept {
    std::size_t count = 0;
    for (const auto& contract : kOot3dFrontendNativeOperationContracts) {
        count += contract.invocation == invocation;
    }
    return count;
}

constexpr bool EntriesAndKindsAreUnique() noexcept {
    for (std::size_t left = 0;
         left < kOot3dFrontendNativeOperationContracts.size(); ++left) {
        if (static_cast<std::size_t>(
                kOot3dFrontendNativeOperationContracts[left].kind) != left) {
            return false;
        }
        for (std::size_t right = left + 1;
             right < kOot3dFrontendNativeOperationContracts.size(); ++right) {
            if (kOot3dFrontendNativeOperationContracts[left].guest_entry ==
                    kOot3dFrontendNativeOperationContracts[right].guest_entry ||
                kOot3dFrontendNativeOperationContracts[left].kind ==
                    kOot3dFrontendNativeOperationContracts[right].kind) {
                return false;
            }
        }
    }
    return true;
}

template <typename Operation>
constexpr UiFrontendNativeOperationKind OperationKindFor() noexcept {
    if constexpr (std::is_same_v<Operation, CopyFileSlotRequest>) {
        return UiFrontendNativeOperationKind::CopyFileSlot;
    } else if constexpr (std::is_same_v<Operation, DeleteFileSlotRequest>) {
        return UiFrontendNativeOperationKind::DeleteFileSlot;
    } else if constexpr (std::is_same_v<Operation, LoadFileSlotRequest>) {
        return UiFrontendNativeOperationKind::LoadFileSlot;
    } else if constexpr (
        std::is_same_v<Operation, CancelFileSlotWriteOperation>) {
        return UiFrontendNativeOperationKind::CancelFileSlotWrite;
    } else if constexpr (
        std::is_same_v<Operation, CancelFileSlotDeleteOperation>) {
        return UiFrontendNativeOperationKind::CancelFileSlotDelete;
    } else if constexpr (
        std::is_same_v<Operation, RefreshFileSlotCatalogOperation>) {
        return UiFrontendNativeOperationKind::RefreshFileSlotCatalog;
    } else {
        static_assert(
            std::is_same_v<Operation, ContinueNameEntrySaveCommitOperation>);
        return UiFrontendNativeOperationKind::ContinueNameEntrySaveCommit;
    }
}

template <typename Variant>
UiFrontendNativeOperationKind OperationKindOfVariant(
    const Variant& operation) noexcept {
    return std::visit(
        [](const auto& value) {
            return OperationKindFor<std::decay_t<decltype(value)>>();
        },
        operation);
}

static_assert(std::variant_size_v<UiFrontendNativeActionRequest> ==
              kUiFrontendNativeActionRequestCount);
static_assert(std::variant_size_v<UiFrontendNativeInternalOperation> ==
              kUiFrontendNativeInternalOperationCount);
static_assert(std::variant_size_v<UiFrontendNativeOperation> ==
              kUiFrontendNativeOperationContractCount);
static_assert(ContractBytes() == 1996,
              "front-end native operation byte coverage drifted");
static_assert(InvocationCount(UiFrontendNativeInvocation::BackendRequest) == 3);
static_assert(InvocationCount(
                  UiFrontendNativeInvocation::NativeFrameContinuation) == 1);
static_assert(InvocationCount(UiFrontendNativeInvocation::NativeMaintenance) == 3);
static_assert(EntriesAndKindsAreUnique());

} // namespace

const std::array<UiFrontendNativeOperationContract,
                 kUiFrontendNativeOperationContractCount>&
Oot3dFrontendNativeOperationContracts() noexcept {
    return kOot3dFrontendNativeOperationContracts;
}

UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeActionRequest& request) noexcept {
    return OperationKindOfVariant(request);
}

UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeInternalOperation& operation) noexcept {
    return OperationKindOfVariant(operation);
}

UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeOperation& operation) noexcept {
    return OperationKindOfVariant(operation);
}

const UiFrontendNativeOperationContract*
FindOot3dFrontendNativeOperationContract(
    UiFrontendNativeOperationKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= kOot3dFrontendNativeOperationContracts.size()) {
        return nullptr;
    }
    return &kOot3dFrontendNativeOperationContracts[index];
}

bool IsUiFrontendNativeOperationRequestableByBackend(
    UiFrontendNativeOperationKind kind) noexcept {
    const UiFrontendNativeOperationContract* contract =
        FindOot3dFrontendNativeOperationContract(kind);
    return contract != nullptr &&
           contract->invocation == UiFrontendNativeInvocation::BackendRequest;
}

bool IsValidUiFrontendNativeActionRequest(
    const UiFrontendNativeActionRequest& request) noexcept {
    return std::visit(
        [](const auto& value) {
            using Request = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Request, CopyFileSlotRequest>) {
                return value.source.IsValid() && value.target.IsValid() &&
                       value.source.Raw() != value.target.Raw();
            } else {
                return value.slot.IsValid();
            }
        },
        request);
}

const char* UiFrontendNativeOperationKindName(
    UiFrontendNativeOperationKind kind) noexcept {
    switch (kind) {
    case UiFrontendNativeOperationKind::CopyFileSlot: return "copy_file_slot";
    case UiFrontendNativeOperationKind::DeleteFileSlot: return "delete_file_slot";
    case UiFrontendNativeOperationKind::LoadFileSlot: return "load_file_slot";
    case UiFrontendNativeOperationKind::CancelFileSlotWrite:
        return "cancel_file_slot_write";
    case UiFrontendNativeOperationKind::CancelFileSlotDelete:
        return "cancel_file_slot_delete";
    case UiFrontendNativeOperationKind::RefreshFileSlotCatalog:
        return "refresh_file_slot_catalog";
    case UiFrontendNativeOperationKind::ContinueNameEntrySaveCommit:
        return "continue_name_entry_save_commit";
    case UiFrontendNativeOperationKind::Count: return "invalid";
    }
    return "invalid";
}

const char* UiFrontendNativeInvocationName(
    UiFrontendNativeInvocation invocation) noexcept {
    switch (invocation) {
    case UiFrontendNativeInvocation::BackendRequest: return "backend_request";
    case UiFrontendNativeInvocation::NativeFrameContinuation:
        return "native_frame_continuation";
    case UiFrontendNativeInvocation::NativeMaintenance:
        return "native_maintenance";
    }
    return "invalid";
}

} // namespace oot3d::ui
