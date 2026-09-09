#pragma once

#include "oot3d_ui/ui_backend.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace oot3d::ui {

// Whole-body ownership is deliberately stricter than semantic membership.
// Composite functions must first be split around their native state mutations.
enum class UiGameplayHudNativeRole : std::uint8_t {
    FrameOwner = 0,
    InitializationOwner,
    RetainedComposite,
    ReplaceableMechanic,
    InternalMechanicHelper,
    RetainedActionSink,
};

enum class UiGameplayHudInvocation : std::uint8_t {
    DirectCall = 0,
    DirectTailCall,
};

enum class UiGameplayHudEdgeKind : std::uint8_t {
    ReplaceableMechanicHandoff = 0,
    InternalMechanicHandoff,
    RetainedCompositeHandoff,
    RetainedActionHandoff,
    RetainedQueryHandoff,
    ExistingUiRootHandoff,
    NativeDependency,
};

enum class UiGameplayHudPresentationDisposition : std::uint8_t {
    ReplaceableRoot = 0,
    RetainedComposite,
};

struct UiGameplayHudFunctionDescriptor {
    std::uint32_t entry = 0;
    std::uint16_t body_bytes = 0;
    const char* name = nullptr;
    const char* semantic_effect = nullptr;
    UiSemanticMechanic mechanic = UiSemanticMechanic::GameplayFrame;
    UiSeamPhase phase = UiSeamPhase::Update;
    UiGameplayHudNativeRole role = UiGameplayHudNativeRole::RetainedComposite;
    std::uint8_t responsibilities = 0;

    constexpr bool IsWholeBodyReplacementRoot() const noexcept {
        return role == UiGameplayHudNativeRole::ReplaceableMechanic;
    }

    constexpr bool RequiresSplitBeforeReplacement() const noexcept {
        return role == UiGameplayHudNativeRole::RetainedComposite;
    }

    constexpr bool IsRetainedActionSink() const noexcept {
        return role == UiGameplayHudNativeRole::RetainedActionSink;
    }

    constexpr bool IsInternalMechanicHelper() const noexcept {
        return role == UiGameplayHudNativeRole::InternalMechanicHelper;
    }
};

struct UiGameplayHudEdgeDescriptor {
    std::uint32_t call_site = 0;
    std::uint32_t call_word = 0;
    const char* call_condition = nullptr;
    UiGameplayHudInvocation invocation = UiGameplayHudInvocation::DirectCall;
    std::uint8_t caller_sequence = 0;
    std::uint32_t caller_entry = 0;
    const char* caller_name = nullptr;
    std::uint32_t target_entry = 0;
    const char* target_name = nullptr;
    UiGameplayHudEdgeKind kind = UiGameplayHudEdgeKind::NativeDependency;

    constexpr bool CrossesReplacementBoundary() const noexcept {
        return kind != UiGameplayHudEdgeKind::NativeDependency &&
               kind != UiGameplayHudEdgeKind::InternalMechanicHandoff;
    }
};

struct UiGameplayHudPresentationRootDescriptor {
    std::uint32_t entry = 0;
    std::uint16_t body_bytes = 0;
    const char* name = nullptr;
    UiSubsystem subsystem = UiSubsystem::GameplayHud;
    UiSeamPhase phase = UiSeamPhase::Draw;
    UiGameplayHudPresentationDisposition disposition =
        UiGameplayHudPresentationDisposition::ReplaceableRoot;
    const char* semantic_effect = nullptr;
};

inline constexpr std::size_t kOot3dGameplayHudFunctionCount = 17;
inline constexpr std::size_t kOot3dGameplayHudEdgeCount = 160;
inline constexpr std::size_t kOot3dGameplayHudPresentationRootCount = 5;

const std::array<UiGameplayHudFunctionDescriptor,
                 kOot3dGameplayHudFunctionCount>&
Oot3dGameplayHudFunctions() noexcept;

const UiGameplayHudFunctionDescriptor* Oot3dGameplayHudFunction(
    std::uint32_t entry) noexcept;

const std::array<UiGameplayHudEdgeDescriptor,
                 kOot3dGameplayHudEdgeCount>&
Oot3dGameplayHudEdges() noexcept;

const UiGameplayHudEdgeDescriptor* Oot3dGameplayHudEdgeAt(
    std::uint32_t call_site) noexcept;

const std::array<UiGameplayHudPresentationRootDescriptor,
                 kOot3dGameplayHudPresentationRootCount>&
Oot3dGameplayHudPresentationRoots() noexcept;

} // namespace oot3d::ui
