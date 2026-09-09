#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_frontend_menu_content.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace oot3d::ui {

// File selection owns save-file I/O even when a future backend owns its
// navigation or presentation. These values describe requests only: the current
// runtime has no dispatcher and the native profile emits none.
enum class UiFrontendNativeOperationKind : std::uint8_t {
    CopyFileSlot,
    DeleteFileSlot,
    LoadFileSlot,
    CancelFileSlotWrite,
    CancelFileSlotDelete,
    RefreshFileSlotCatalog,
    ContinueNameEntrySaveCommit,
    Count,
};

enum class UiFrontendNativeInvocation : std::uint8_t {
    BackendRequest,
    NativeFrameContinuation,
    NativeMaintenance,
};

struct CopyFileSlotRequest {
    UiFileSelectSlotIndex source;
    UiFileSelectSlotIndex target;
};

struct DeleteFileSlotRequest {
    UiFileSelectSlotIndex slot;
};

struct LoadFileSlotRequest {
    UiFileSelectSlotIndex slot;
};

struct CancelFileSlotWriteOperation {};
struct CancelFileSlotDeleteOperation {};
struct RefreshFileSlotCatalogOperation {};
struct ContinueNameEntrySaveCommitOperation {};

using UiFrontendNativeActionRequest = std::variant<
    CopyFileSlotRequest,
    DeleteFileSlotRequest,
    LoadFileSlotRequest>;

using UiFrontendNativeInternalOperation = std::variant<
    CancelFileSlotWriteOperation,
    CancelFileSlotDeleteOperation,
    RefreshFileSlotCatalogOperation,
    ContinueNameEntrySaveCommitOperation>;

using UiFrontendNativeOperation = std::variant<
    CopyFileSlotRequest,
    DeleteFileSlotRequest,
    LoadFileSlotRequest,
    CancelFileSlotWriteOperation,
    CancelFileSlotDeleteOperation,
    RefreshFileSlotCatalogOperation,
    ContinueNameEntrySaveCommitOperation>;

struct UiFrontendNativeOperationContract {
    UiFrontendNativeOperationKind kind =
        UiFrontendNativeOperationKind::CopyFileSlot;
    std::uint32_t guest_entry = 0;
    std::uint32_t guest_size = 0;
    UiSubsystem subsystem = UiSubsystem::FileSelect;
    UiSemanticMechanic mechanic = UiSemanticMechanic::FileSelection;
    UiFrontendNativeInvocation invocation =
        UiFrontendNativeInvocation::BackendRequest;
    bool requires_native_play_state = false;
};

inline constexpr std::size_t kUiFrontendNativeOperationContractCount = 7;
inline constexpr std::size_t kUiFrontendNativeActionRequestCount = 3;
inline constexpr std::size_t kUiFrontendNativeInternalOperationCount = 4;

const std::array<UiFrontendNativeOperationContract,
                 kUiFrontendNativeOperationContractCount>&
Oot3dFrontendNativeOperationContracts() noexcept;
UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeActionRequest& request) noexcept;
UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeInternalOperation& operation) noexcept;
UiFrontendNativeOperationKind UiFrontendNativeOperationKindOf(
    const UiFrontendNativeOperation& operation) noexcept;
const UiFrontendNativeOperationContract*
FindOot3dFrontendNativeOperationContract(
    UiFrontendNativeOperationKind kind) noexcept;
bool IsUiFrontendNativeOperationRequestableByBackend(
    UiFrontendNativeOperationKind kind) noexcept;
// Validates only the lossless request shape (visible 0..2 indices and distinct
// copy endpoints). A future native adapter must additionally validate current
// slot validity, controller phase, and outstanding asynchronous I/O state.
bool IsValidUiFrontendNativeActionRequest(
    const UiFrontendNativeActionRequest& request) noexcept;
const char* UiFrontendNativeOperationKindName(
    UiFrontendNativeOperationKind kind) noexcept;
const char* UiFrontendNativeInvocationName(
    UiFrontendNativeInvocation invocation) noexcept;

} // namespace oot3d::ui
