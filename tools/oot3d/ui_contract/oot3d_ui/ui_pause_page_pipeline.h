#pragma once

#include "oot3d_ui/ui_contract_types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// Page-level boundaries are intentionally coarser than render lanes. A future
// backend can first replace a complete page here, then use the finer Gear and
// Quest render-lane contracts for a gradual transition.
enum class UiPausePipelineComponent : std::uint8_t {
    PauseRoot = 0,
    TouchControls,
    Items,
    Equipment,
    DungeonMap,
    WorldMap,
    SystemMenu,
    FileSelect,
    NameEntry,
    CameraControl,
    TextureControl,
    DetailPanel,
    QuestStatus,
    OmoteUraSelector,
    Overlay,
    Count,
};

enum class UiPausePipelinePresentationMode : std::uint8_t {
    DiscreteRoots,
    UpdateAndSubmitFused,
    UpdateOwnsPresentation,
    DrawOwnsPresentation,
    RendererLocalDrawOnly,
};

enum class UiPausePipelineLifetime : std::uint8_t {
    ProcessGlobalInline,
    ProcessGlobalIndirect,
    RendererOwned,
};

enum class UiPausePipelinePhase : std::uint8_t {
    FrameUpdate,
    InputUpdate,
    PageUpdate,
    InputAndResources,
    Submit,
    ActivityGate,
    ViewPass,
    DrawGate,
    Draw,
    Callback,
    StateReset,
    SharedResourceRelease,
};

enum class UiPausePipelineInvocation : std::uint8_t {
    DirectCall,
    VirtualCall,
};

struct UiPausePipelineComponentDescriptor {
    UiPausePipelineComponent component = UiPausePipelineComponent::Count;
    UiSubsystem subsystem = UiSubsystem::PauseShell;
    const char* semantic_role = nullptr;
    const char* owner_name = nullptr;
    std::uint32_t owner_address = 0;
    std::uint32_t owner_size = 0;
    UiPausePipelineLifetime lifetime =
        UiPausePipelineLifetime::ProcessGlobalInline;
    UiPausePipelinePresentationMode presentation_mode =
        UiPausePipelinePresentationMode::DiscreteRoots;
    std::uint32_t update_entry = 0;
    const char* update_name = nullptr;
    std::uint32_t submit_entry = 0;
    const char* submit_name = nullptr;
    std::uint32_t draw_entry = 0;
    const char* draw_name = nullptr;
    bool page_close_releases_owner = false;

    constexpr bool HasNativeUpdate() const noexcept {
        return update_entry != 0;
    }

    constexpr bool HasNativeSubmit() const noexcept {
        return submit_entry != 0;
    }

    constexpr bool HasNativeDraw() const noexcept {
        return draw_entry != 0;
    }

    constexpr bool OwnsIntegratedPresentation() const noexcept {
        return presentation_mode ==
                   UiPausePipelinePresentationMode::UpdateOwnsPresentation ||
               presentation_mode ==
                   UiPausePipelinePresentationMode::DrawOwnsPresentation ||
               presentation_mode ==
                   UiPausePipelinePresentationMode::UpdateAndSubmitFused;
    }
};

struct UiPausePipelineOperationDescriptor {
    UiPausePipelineComponent component = UiPausePipelineComponent::Count;
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    std::uint32_t owner_entry = 0;
    const char* owner_name = nullptr;
    std::uint16_t owner_body_bytes = 0;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiPausePipelinePhase phase = UiPausePipelinePhase::PageUpdate;
    UiPausePipelineInvocation invocation =
        UiPausePipelineInvocation::DirectCall;
    std::uint8_t owner_sequence = 0;
    const char* native_guard = nullptr;
    const char* semantic_effect = nullptr;
    bool replacement_boundary = false;

    constexpr bool IsPresentationPhase() const noexcept {
        return phase == UiPausePipelinePhase::Submit ||
               phase == UiPausePipelinePhase::ViewPass ||
               phase == UiPausePipelinePhase::Draw;
    }
};

inline constexpr std::size_t kOot3dPausePipelineComponentCount = 15;
inline constexpr std::size_t kOot3dPausePipelineOperationCount = 57;
inline constexpr std::size_t kOot3dPausePipelineFunctionCount = 40;

const std::array<UiPausePipelineComponentDescriptor,
                 kOot3dPausePipelineComponentCount>&
Oot3dPausePipelineComponents() noexcept;

const UiPausePipelineComponentDescriptor* Oot3dPausePipelineComponent(
    UiPausePipelineComponent component) noexcept;

const std::array<UiPausePipelineOperationDescriptor,
                 kOot3dPausePipelineOperationCount>&
Oot3dPausePipelineOperations() noexcept;

const UiPausePipelineOperationDescriptor* Oot3dPausePipelineOperationAt(
    std::uint32_t call_site) noexcept;

static_assert(static_cast<std::size_t>(UiPausePipelineComponent::Count) ==
              kOot3dPausePipelineComponentCount);

} // namespace oot3d::ui
