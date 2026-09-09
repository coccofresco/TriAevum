#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class UiNativeWorkflowDisposition : std::uint8_t {
    UiWorkflow = 0,
    BackendRequest,
    NativeInternal,
    NativeQuery,
};

// A function can belong to several reviewed catalogs. The mask preserves that
// evidence without inventing one exclusive native owner for shared helpers.
enum class UiNativeWorkflowGroup : std::uint8_t {
    PauseItems = 0,
    PauseMap,
    PauseQuest,
    SystemMenu,
    PauseOrchestration,
    Frontend,
    NativeAction,
    NativeQuery,
    BaseSeam,
    PagePipeline,
    ComponentLifecycle,
};

using UiNativeWorkflowGroupMask = std::uint16_t;

constexpr UiNativeWorkflowGroupMask UiNativeWorkflowGroupBit(
    UiNativeWorkflowGroup group) noexcept {
    return static_cast<UiNativeWorkflowGroupMask>(
        1U << static_cast<std::uint8_t>(group));
}

enum class UiNativeWorkflowInvocation : std::uint8_t {
    DirectCall = 0,
    DirectTailCall,
};

enum class UiNativeWorkflowEdgeKind : std::uint8_t {
    UiWorkflowInternal = 0,
    HandoffToBackendRequest,
    HandoffToNativeInternal,
    HandoffToNativeQuery,
    NativeReentry,
    NativeBoundaryInternal,
};

struct UiNativeWorkflowFunctionDescriptor {
    std::uint32_t entry = 0;
    std::uint16_t body_bytes = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
    UiNativeWorkflowGroupMask groups = 0;
    UiNativeWorkflowDisposition disposition =
        UiNativeWorkflowDisposition::UiWorkflow;

    constexpr bool HasGroup(UiNativeWorkflowGroup group) const noexcept {
        return (groups & UiNativeWorkflowGroupBit(group)) != 0;
    }

    constexpr bool RequiresUiParity() const noexcept {
        return disposition == UiNativeWorkflowDisposition::UiWorkflow;
    }

    constexpr bool IsRetainedNativeBoundary() const noexcept {
        return !RequiresUiParity();
    }
};

struct UiNativeWorkflowEdgeDescriptor {
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    UiNativeWorkflowInvocation invocation =
        UiNativeWorkflowInvocation::DirectCall;
    std::uint16_t caller_sequence = 0;
    std::uint32_t caller_entry = 0;
    const char* caller_name = nullptr;
    UiNativeWorkflowDisposition caller_disposition =
        UiNativeWorkflowDisposition::UiWorkflow;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiNativeWorkflowDisposition target_disposition =
        UiNativeWorkflowDisposition::UiWorkflow;
    UiNativeWorkflowEdgeKind kind =
        UiNativeWorkflowEdgeKind::UiWorkflowInternal;

    constexpr bool IsRetainedHandoff() const noexcept {
        return kind == UiNativeWorkflowEdgeKind::HandoffToBackendRequest ||
               kind == UiNativeWorkflowEdgeKind::HandoffToNativeInternal ||
               kind == UiNativeWorkflowEdgeKind::HandoffToNativeQuery;
    }

    constexpr bool IsNativeReentry() const noexcept {
        return kind == UiNativeWorkflowEdgeKind::NativeReentry;
    }
};

inline constexpr std::size_t kOot3dNativeWorkflowFunctionCount = 264;
inline constexpr std::size_t kOot3dNativeWorkflowEdgeCount = 1348;

const std::array<UiNativeWorkflowFunctionDescriptor,
                 kOot3dNativeWorkflowFunctionCount>&
Oot3dNativeWorkflowFunctions() noexcept;

const UiNativeWorkflowFunctionDescriptor* Oot3dNativeWorkflowFunction(
    std::uint32_t entry) noexcept;

const std::array<UiNativeWorkflowEdgeDescriptor,
                 kOot3dNativeWorkflowEdgeCount>&
Oot3dNativeWorkflowEdges() noexcept;

const UiNativeWorkflowEdgeDescriptor* Oot3dNativeWorkflowEdgeAt(
    std::uint32_t call_site) noexcept;

} // namespace oot3d::ui
