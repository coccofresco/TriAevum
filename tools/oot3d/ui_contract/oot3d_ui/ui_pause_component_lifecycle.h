#pragma once

#include "oot3d_ui/ui_pause_page_pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// These stages describe the original component lifetime. They do not authorize
// a backend to allocate, reset, or release guest-owned state.
enum class UiPauseLifecycleStage : std::uint8_t {
    Bootstrap = 0,
    Create,
    Initialize,
    LoadResources,
    Activate,
    Deactivate,
    Reset,
    ResetAndRelease,
    ReleaseResources,
    Close,
    CloseAndActivate,
    Destroy,
};

enum class UiPauseLifecycleEntryRole : std::uint8_t {
    OrchestrationRoot = 0,
    ComponentRoot,
    ComponentStep,
    SharedHelper,
};

enum class UiPauseLifecycleInvocation : std::uint8_t {
    DirectCall = 0,
    DirectTailCall,
};

struct UiPauseLifecycleFunctionDescriptor {
    UiPausePipelineComponent component = UiPausePipelineComponent::Count;
    std::uint32_t entry = 0;
    const char* name = nullptr;
    std::uint16_t body_bytes = 0;
    UiPauseLifecycleStage stage = UiPauseLifecycleStage::Initialize;
    UiPauseLifecycleEntryRole role =
        UiPauseLifecycleEntryRole::ComponentStep;
    const char* semantic_effect = nullptr;
    bool replacement_entry = false;
    bool releases_owner_storage = false;

    constexpr bool IsLifecycleRoot() const noexcept {
        return role == UiPauseLifecycleEntryRole::OrchestrationRoot ||
               role == UiPauseLifecycleEntryRole::ComponentRoot;
    }
};

struct UiPauseLifecycleEdgeDescriptor {
    UiPausePipelineComponent component = UiPausePipelineComponent::Count;
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    std::uint32_t caller_entry = 0;
    const char* caller_name = nullptr;
    std::uint16_t caller_body_bytes = 0;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiPauseLifecycleStage target_stage = UiPauseLifecycleStage::Initialize;
    UiPauseLifecycleInvocation invocation =
        UiPauseLifecycleInvocation::DirectCall;
    std::uint8_t caller_sequence = 0;
    const char* semantic_effect = nullptr;
    bool crosses_component = false;
    bool replacement_boundary = false;
};

inline constexpr std::size_t kOot3dPauseLifecycleFunctionCount = 61;
inline constexpr std::size_t kOot3dPauseLifecycleEdgeCount = 65;

const std::array<UiPauseLifecycleFunctionDescriptor,
                 kOot3dPauseLifecycleFunctionCount>&
Oot3dPauseLifecycleFunctions() noexcept;

const UiPauseLifecycleFunctionDescriptor* Oot3dPauseLifecycleFunction(
    std::uint32_t entry) noexcept;

const std::array<UiPauseLifecycleEdgeDescriptor,
                 kOot3dPauseLifecycleEdgeCount>&
Oot3dPauseLifecycleEdges() noexcept;

const UiPauseLifecycleEdgeDescriptor* Oot3dPauseLifecycleEdgeAt(
    std::uint32_t call_site) noexcept;

} // namespace oot3d::ui
