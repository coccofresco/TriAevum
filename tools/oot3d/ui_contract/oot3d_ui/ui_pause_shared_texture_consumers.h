#pragma once

#include "oot3d_ui/ui_contract_types.h"
#include "oot3d_ui/ui_localized_menu_resources.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// Describes what the original OoT3D caller does with the descriptor returned
// from RendererTextureDescriptor_GetBuiltin. This is evidence about the native
// renderer path, not a host-side replacement policy.
enum class UiPauseSharedTextureResultUse : std::uint8_t {
    TextureStageBinding,
    SceneResourceGroupSlot,
    OwnerDescriptorSlot,
    LocalRendererState,
    DiscardedLookup,
    Count,
};

struct UiPauseSharedTextureConsumerDescriptor {
    std::uint32_t call_site = 0;
    std::uint32_t native_function_entry = 0;
    std::uint32_t native_function_size = 0;
    UiPauseSharedTextureSlot slot = UiPauseSharedTextureSlot::Count;
    // Count means that the native owner is outside the currently routable UI
    // subsystem set (currently only HintMovie uses that state).
    UiSubsystem substitution_subsystem = UiSubsystem::Count;
    UiPauseSharedTextureResultUse result_use =
        UiPauseSharedTextureResultUse::TextureStageBinding;
    const char* native_function = nullptr;
    const char* native_role = nullptr;

    constexpr bool IsBackendRouted() const noexcept {
        return substitution_subsystem != UiSubsystem::Count;
    }
};

inline constexpr std::size_t kOot3dPauseSharedTextureConsumerCount = 34;
inline constexpr std::size_t kOot3dPauseSharedTextureConsumerFunctionCount = 26;

const std::array<UiPauseSharedTextureConsumerDescriptor,
                 kOot3dPauseSharedTextureConsumerCount>&
Oot3dPauseSharedTextureConsumers() noexcept;

const UiPauseSharedTextureConsumerDescriptor*
Oot3dPauseSharedTextureConsumerForCallSite(std::uint32_t call_site) noexcept;

std::size_t Oot3dPauseSharedTextureConsumerCountFor(
    UiPauseSharedTextureSlot slot) noexcept;

const UiPauseSharedTextureConsumerDescriptor* Oot3dPauseSharedTextureConsumerFor(
    UiPauseSharedTextureSlot slot, std::size_t ordinal) noexcept;

static_assert(kOot3dPauseSharedTextureCount == 16);

} // namespace oot3d::ui
