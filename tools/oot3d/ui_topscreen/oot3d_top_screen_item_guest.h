#pragma once

#include "oot3d_native_a32_memory.h"
#include "oot3d_top_screen_mod_profile.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Oot3dNativeGame {

// Resolves one of the four original GlobalActionState boolean getters replaced
// by the mod. Unknown entries or unavailable guest state return no value so
// the caller can execute the original function unchanged.
std::optional<bool> ResolveTopScreenItemQueryGuest(
    NativeA32Memory& memory, std::uint32_t originalEntry,
    const TopScreenExtendedInputFrame& input);

// Returns only the mod's compatibility override for slots 3/4. Native special
// state and every ordinary case return no value and retain the original OoT3D
// resolver, including its inventory and fallback behavior.
std::optional<std::uint8_t> ResolveTopScreenSlotItemOverrideGuest(
    NativeA32Memory& memory, std::uint32_t globalContext,
    std::uint8_t slot, const TopScreenExtendedInputFrame& input);

struct TopScreenAimProjectileCycleInput {
    bool PreviousPressed = false;
    bool NextPressed = false;
};

struct TopScreenAimProjectileCycleResult {
    bool Eligible = false;
    std::uint32_t InitialSelector = 0;
    std::uint32_t FinalSelector = 0;
    std::uint32_t Updates = 0;
    std::uint32_t PromptWrites = 0;
    std::uint32_t LastPrompt = 0;
};

// Applies the TopScreen 1.2 aiming-selector delta to the original OoT3D state.
// The caller supplies fresh D-pad Left/Right edges and may map Restoration ZL
// to the next edge. Audio remains a caller-owned native side effect.
bool ApplyTopScreenAimProjectileCycleGuest(
    NativeA32Memory& memory, const TopScreenAimProjectileCycleInput& input,
    TopScreenAimProjectileCycleResult* result,
    std::string* error = nullptr);

struct TopScreenGameplayDpadInput {
    bool ViewPressed = false;
    bool OcarinaPressed = false;
    bool LeftShoulderHeld = false;
    bool RightShoulderHeld = false;
};

struct TopScreenGameplayDpadResult {
    bool EligibleScene = false;
    bool ShoulderChordSuppressed = false;
    bool NaviViewActivated = false;
    bool OcarinaQueryRequired = false;
    bool OcarinaActivated = false;
};

// Applies the payload's mapped Navi/view action first, then exposes the exact
// point at which its mapped Ocarina action calls the original OoT3D
// eligibility function. Physical D-pad direction is resolved separately.
bool PrepareTopScreenGameplayDpadGuest(
    NativeA32Memory& memory, const TopScreenGameplayDpadInput& input,
    TopScreenGameplayDpadResult* result, std::string* error = nullptr);

bool CommitTopScreenGameplayOcarinaGuest(
    NativeA32Memory& memory, std::uint32_t nativeEligibility,
    TopScreenGameplayDpadResult* result, std::string* error = nullptr);

} // namespace Oot3dNativeGame
