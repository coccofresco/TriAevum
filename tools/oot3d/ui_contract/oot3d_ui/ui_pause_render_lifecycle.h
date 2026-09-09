#pragma once

#include "oot3d_ui/ui_pause_native_geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// One lane is one independently replaceable native presentation resource. A
// lane may own geometry, a model, an icon helper, or a nested renderer group.
enum class UiPauseRenderLane : std::uint8_t {
    GearMain = 0,
    GearPrimaryIconSlot,
    GearSlotIcons,
    GearAuxiliary0,
    GearAuxiliary1,
    GearAuxiliaryIcons,
    GearQuadOverlay,
    QuestMain,
    QuestIcon,
    QuestPrimaryCounter,
    QuestSecondaryCounter,
    QuestModelPreview,
    QuestDualPrimary,
    QuestDualSecondary,
    QuestSingle,
    QuestIcons,
    QuestTokenGroup,
    QuestTokenLeftDigits,
    QuestTokenRightDigits,
    QuestOptionalPanelModel,
    QuestOptionalPanelQuad0,
    QuestOptionalPanelQuad1,
    Count,
};

enum class UiPauseRenderResourceKind : std::uint8_t {
    StreamModel,
    Model,
    IconSlot,
    IconGroup,
    QuadGroup,
    SkeletonQueue,
};

enum class UiPauseRenderPhase : std::uint8_t {
    SubmitDispatch,
    StreamUpload,
    Submit,
    ContentRefresh,
    DrawDispatch,
    Draw,
};

enum class UiPauseRenderOperationKind : std::uint8_t {
    DispatchSubmit,
    ApplyPositionOffsets,
    UploadPositions,
    UploadTexcoords,
    UploadColors,
    SubmitModel,
    SubmitGroup,
    RefreshIcons,
    QueueSkeletonModel,
    DispatchDraw,
    DrawModel,
    DrawIconSlot,
    DrawIconGroup,
    DrawQuadGroup,
};

enum class UiPauseRenderInvocation : std::uint8_t {
    DirectCall,
    VirtualCall,
    DirectTailCall,
    VirtualTailCall,
};

inline constexpr std::int16_t kUiPauseRenderNoOffset = -1;

struct UiPauseRenderLaneDescriptor {
    UiPauseRenderLane lane = UiPauseRenderLane::Count;
    UiSubsystem subsystem = UiSubsystem::PauseShell;
    UiPauseRenderResourceKind resource_kind =
        UiPauseRenderResourceKind::Model;
    UiPauseNativeGeometryProfile geometry_profile =
        UiPauseNativeGeometryProfile::Count;
    const char* semantic_role = nullptr;
    const char* owner_name = nullptr;
    std::uint32_t owner_address = 0;
    std::int16_t container_offset = kUiPauseRenderNoOffset;
    std::int16_t stream_binding_offset = kUiPauseRenderNoOffset;
    std::int16_t render_binding_offset = kUiPauseRenderNoOffset;
    std::int16_t geometry_binding_offset = kUiPauseRenderNoOffset;
    std::uint8_t submit_operation_count = 0;
    std::uint8_t draw_operation_count = 0;

    constexpr bool HasNativeGeometryProfile() const noexcept {
        return geometry_profile != UiPauseNativeGeometryProfile::Count;
    }

    constexpr bool HasSubmitPath() const noexcept {
        return submit_operation_count != 0;
    }

    constexpr bool HasDrawPath() const noexcept {
        return draw_operation_count != 0;
    }
};

constexpr std::uint32_t UiPauseRenderLaneBit(
    UiPauseRenderLane lane) noexcept {
    const auto index = static_cast<std::uint32_t>(lane);
    return index < 32U ? (1U << index) : 0U;
}

struct UiPauseRenderOperationDescriptor {
    std::uint32_t lane_mask = 0;
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    std::uint32_t owner_entry = 0;
    const char* owner_name = nullptr;
    std::uint16_t owner_body_bytes = 0;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiPauseRenderPhase phase = UiPauseRenderPhase::Submit;
    UiPauseRenderOperationKind kind =
        UiPauseRenderOperationKind::SubmitModel;
    UiPauseRenderInvocation invocation =
        UiPauseRenderInvocation::DirectCall;
    std::uint8_t native_multiplicity = 1;
    const char* native_guard = nullptr;
    const char* semantic_effect = nullptr;

    constexpr bool Affects(UiPauseRenderLane candidate) const noexcept {
        return (lane_mask & UiPauseRenderLaneBit(candidate)) != 0;
    }

    constexpr bool IsDrawPhase() const noexcept {
        return phase == UiPauseRenderPhase::DrawDispatch ||
               phase == UiPauseRenderPhase::Draw;
    }
};

inline constexpr std::size_t kOot3dPauseRenderLaneCount = 22;
inline constexpr std::size_t kOot3dPauseRenderOperationCount = 65;
inline constexpr std::size_t kOot3dPauseRenderFunctionCount = 12;

const std::array<UiPauseRenderLaneDescriptor,
                 kOot3dPauseRenderLaneCount>&
Oot3dPauseRenderLanes() noexcept;

const UiPauseRenderLaneDescriptor* Oot3dPauseRenderLane(
    UiPauseRenderLane lane) noexcept;

const std::array<UiPauseRenderOperationDescriptor,
                 kOot3dPauseRenderOperationCount>&
Oot3dPauseRenderOperations() noexcept;

const UiPauseRenderOperationDescriptor* Oot3dPauseRenderOperationAt(
    std::uint32_t call_site) noexcept;

static_assert(static_cast<std::size_t>(UiPauseRenderLane::Count) ==
              kOot3dPauseRenderLaneCount);

} // namespace oot3d::ui
