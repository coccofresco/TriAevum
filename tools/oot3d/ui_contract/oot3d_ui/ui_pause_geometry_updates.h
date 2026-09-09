#pragma once

#include "oot3d_ui/ui_pause_native_geometry.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class UiPauseGeometryBindingKind : std::uint8_t {
    RenderBufferPointer,
    ConstructorPrivateStreams,
};

enum class UiPauseGeometryMutationKind : std::uint8_t {
    InitializeRectPositions,
    InitializeTextureCoordinates,
    InitializeAlpha,
    OffsetPositions,
    ReplaceQuadPositions,
    ReplaceQuadRects,
    UpdateAlpha,
};

enum class UiPauseGeometryValueSource : std::uint8_t {
    OwnerArrays,
    ConstantOne,
    ConstantVec2,
    InventoryUpgradeFormula,
    TransitionFrameFormula,
    SelectionStateFormula,
    RuntimeStateFormula,
    SceneProjectionFormula,
};

enum class UiPauseGeometryExecution : std::uint8_t {
    Once,
    LoopProfiles,
    LoopEntries,
    StateBranch,
    PerFrame,
};

inline constexpr std::int16_t kUiPauseGeometryDynamicEntry = -1;
inline constexpr std::int16_t kUiPauseGeometryNoOffset = -1;

struct UiPauseRuntimeGeometryProfileDescriptor {
    UiPauseNativeGeometryProfile profile = UiPauseNativeGeometryProfile::Count;
    const char* semantic_role = nullptr;
    const char* owner_name = nullptr;
    std::uint32_t owner_address = 0;
    UiPauseGeometryBindingKind binding_kind =
        UiPauseGeometryBindingKind::RenderBufferPointer;
    std::int16_t renderer_binding_offset = kUiPauseGeometryNoOffset;
    std::int16_t secondary_binding_offset = kUiPauseGeometryNoOffset;
    std::uint32_t stream_storage_base = 0;
    std::int16_t rect_origin_offset = kUiPauseGeometryNoOffset;
    std::int16_t rect_extent_offset = kUiPauseGeometryNoOffset;
    std::int16_t texture_origin_offset = kUiPauseGeometryNoOffset;
    std::int16_t texture_extent_offset = kUiPauseGeometryNoOffset;
    std::uint8_t constructor_mutation_count = 0;
    std::uint8_t post_constructor_mutation_count = 0;

    constexpr bool HasPostConstructorWriter() const noexcept {
        return post_constructor_mutation_count != 0;
    }
};

constexpr std::uint32_t UiPauseGeometryProfileBit(
    UiPauseNativeGeometryProfile profile) noexcept {
    const auto index = static_cast<std::uint32_t>(profile);
    return index < 32U ? (1U << index) : 0U;
}

struct UiPauseGeometryMutationDescriptor {
    std::uint32_t profile_mask = 0;
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    std::uint32_t producer_entry = 0;
    const char* producer_name = nullptr;
    std::uint16_t producer_body_bytes = 0;
    std::uint32_t primitive_entry = 0;
    const char* primitive_name = nullptr;
    UiPauseGeometryMutationKind kind =
        UiPauseGeometryMutationKind::OffsetPositions;
    UiPauseGeometryValueSource value_source =
        UiPauseGeometryValueSource::RuntimeStateFormula;
    UiPauseGeometryExecution execution = UiPauseGeometryExecution::PerFrame;
    std::int16_t first_entry = kUiPauseGeometryDynamicEntry;
    std::int16_t entry_count = kUiPauseGeometryDynamicEntry;
    const char* native_state_values = nullptr;
    const char* semantic_effect = nullptr;
    bool constructor_phase = false;

    constexpr bool Affects(UiPauseNativeGeometryProfile candidate) const noexcept {
        return (profile_mask & UiPauseGeometryProfileBit(candidate)) != 0;
    }
};

inline constexpr std::size_t kOot3dPauseRuntimeGeometryProfileCount = 5;
inline constexpr std::size_t kOot3dPauseGeometryMutationCount = 46;

const std::array<UiPauseRuntimeGeometryProfileDescriptor,
                 kOot3dPauseRuntimeGeometryProfileCount>&
Oot3dPauseRuntimeGeometryProfiles() noexcept;

const UiPauseRuntimeGeometryProfileDescriptor* Oot3dPauseRuntimeGeometryProfile(
    UiPauseNativeGeometryProfile profile) noexcept;

const std::array<UiPauseGeometryMutationDescriptor,
                 kOot3dPauseGeometryMutationCount>&
Oot3dPauseGeometryMutations() noexcept;

const UiPauseGeometryMutationDescriptor* Oot3dPauseGeometryMutationAt(
    std::uint32_t call_site) noexcept;

} // namespace oot3d::ui
