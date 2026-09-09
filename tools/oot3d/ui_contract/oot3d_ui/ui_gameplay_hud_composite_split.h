#pragma once

#include "oot3d_ui/ui_gameplay_hud_workflow.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

enum class UiGameplayHudCompositeDisposition : std::uint8_t {
    ReplaceableMechanic = 0,
    ReplaceablePresentation,
    RetainAction,
    RetainQuery,
    RetainStateMutation,
    SplitComposite,
    PureHelper,
};

enum class UiGameplayHudCompositeLane : std::uint8_t {
    GameplayGate = 0,
    GameplayState,
    GameplayHazard,
    HudAlpha,
    HudButtons,
    HudHealth,
    HudMinimap,
    HudMagic,
    HudVisibility,
    Audio,
    PresentationRuntime,
    PureRuntime,
};

struct UiGameplayHudCompositeFunctionDescriptor {
    std::uint32_t entry = 0;
    std::uint16_t body_bytes = 0;
    const char* name = nullptr;
    std::uint8_t seam_begin = 0;
    std::uint8_t seam_count = 0;
    std::uint8_t replaceable_seam_count = 0;
    std::uint8_t retained_seam_count = 0;
    std::uint8_t pure_helper_seam_count = 0;
    const char* replaceable_scope = nullptr;
    const char* retained_scope = nullptr;
};

struct UiGameplayHudCompositeSeamDescriptor {
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    UiGameplayHudInvocation invocation = UiGameplayHudInvocation::DirectCall;
    std::uint8_t caller_sequence = 0;
    std::uint32_t caller_entry = 0;
    const char* caller_name = nullptr;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiGameplayHudCompositeDisposition disposition =
        UiGameplayHudCompositeDisposition::PureHelper;
    UiGameplayHudCompositeLane lane = UiGameplayHudCompositeLane::PureRuntime;
    const char* semantic_effect = nullptr;
    const char* replacement_obligation = nullptr;

    constexpr bool MovesToReplacementBackend() const noexcept {
        return disposition == UiGameplayHudCompositeDisposition::ReplaceableMechanic ||
               disposition == UiGameplayHudCompositeDisposition::ReplaceablePresentation;
    }

    constexpr bool PreservesNativeOrder() const noexcept {
        return disposition == UiGameplayHudCompositeDisposition::RetainAction ||
               disposition == UiGameplayHudCompositeDisposition::RetainQuery ||
               disposition == UiGameplayHudCompositeDisposition::RetainStateMutation ||
               disposition == UiGameplayHudCompositeDisposition::SplitComposite;
    }
};

inline constexpr std::size_t kOot3dGameplayHudCompositeFunctionCount = 6;
inline constexpr std::size_t kOot3dGameplayHudCompositeSeamCount = 61;

const std::array<UiGameplayHudCompositeFunctionDescriptor,
                 kOot3dGameplayHudCompositeFunctionCount>&
Oot3dGameplayHudCompositeFunctions() noexcept;

const UiGameplayHudCompositeFunctionDescriptor* Oot3dGameplayHudCompositeFunction(
    std::uint32_t entry) noexcept;

const std::array<UiGameplayHudCompositeSeamDescriptor,
                 kOot3dGameplayHudCompositeSeamCount>&
Oot3dGameplayHudCompositeSeams() noexcept;

const UiGameplayHudCompositeSeamDescriptor* Oot3dGameplayHudCompositeSeamAt(
    std::uint32_t call_site) noexcept;

const char* UiGameplayHudCompositeDispositionName(
    UiGameplayHudCompositeDisposition disposition) noexcept;

const char* UiGameplayHudCompositeLaneName(
    UiGameplayHudCompositeLane lane) noexcept;

} // namespace oot3d::ui
