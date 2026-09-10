#include "oot3d_top_screen_item_guest.h"

namespace Oot3dNativeGame {
namespace {

constexpr std::uint32_t kGlobalActionState = 0x0050AF34U;
constexpr std::uint32_t kItemISlotIdentity = 0x005879FCU;
constexpr std::uint32_t kItemIISlotIdentity = 0x005879FDU;
constexpr std::uint32_t kSuppressionFlagsOffset = 0x68U;
constexpr std::uint32_t kSpecialStateOffset = 0x5C75U;
constexpr std::uint32_t kAimPromptState = 0x004FDA6CU;
constexpr std::uint32_t kAimPromptValueOffset = 0x38U;
constexpr std::uint32_t kAimActiveOffset = 0x50U;
constexpr std::uint32_t kAimSelectorState = 0x00506CB0U;
constexpr std::uint32_t kAimSelectorIndexOffset = 0x30U;
constexpr std::uint32_t kAimSelectorModeOffset = 0x38U;
constexpr std::uint32_t kPreviousAvailabilityTable = 0x0055B634U;
constexpr std::uint32_t kNextAvailabilityTable = 0x0055B63CU;
constexpr std::uint32_t kGameplayRoot = 0x005043D4U;
constexpr std::uint32_t kGameplaySceneOffset = 0x0CU;
constexpr std::uint32_t kGameplayActionBlockOffset = 0x18U;
constexpr std::uint32_t kPauseContext = 0x0050AF34U;
constexpr std::uint32_t kPauseStateOffset = 0x34U;
constexpr std::uint32_t kPauseActionOffset = 0x38U;
constexpr std::uint32_t kPauseOcarinaOffset = 0x60U;
constexpr std::uint32_t kPauseFlagsOffset = 0x68U;
constexpr std::uint32_t kPauseNaviModeOffset = 0x70U;
constexpr std::uint32_t kNaviModeOneSignal = 0x00565660U;
constexpr std::uint32_t kSceneModeOffset = 0x100U;
constexpr std::uint32_t kSceneVariantOffset = 0x101U;
constexpr std::uint32_t kSceneExtensionOffset = 0x20ACU;
constexpr std::uint32_t kSceneSpecialOwnerOffset = 0x12B8U;
constexpr std::uint32_t kSceneNaviTextOffset = 0x2DD4U;

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::optional<std::array<bool, 4>> ResolveCompatibilityOverrides(
    NativeA32Memory& memory, const TopScreenExtendedInputFrame& input) {
    std::uint8_t itemI = 0;
    std::uint8_t itemII = 0;
    if (!memory.Read8(kItemISlotIdentity, &itemI) ||
        !memory.Read8(kItemIISlotIdentity, &itemII)) {
        return std::nullopt;
    }
    return BuildTopScreenItemCompatibilityOverrides(input, itemI, itemII);
}

} // namespace

std::optional<bool> ResolveTopScreenItemQueryGuest(
    NativeA32Memory& memory, std::uint32_t originalEntry,
    const TopScreenExtendedInputFrame& input, TopScreenItemQueryState* observed) {
    const TopScreenItemQueryContract* contract = nullptr;
    for (const auto& candidate : TopScreenVerifiedItemQueryContracts()) {
        if (candidate.OriginalEntry == originalEntry) {
            contract = &candidate;
            break;
        }
    }
    if (contract == nullptr) {
        return std::nullopt;
    }

    TopScreenItemQueryState state;
    std::uint32_t nativeResult = 0;
    if (!memory.Read32(kGlobalActionState + contract->NativeFieldOffset,
                       &nativeResult) ||
        !memory.Read32(kGlobalActionState + kSuppressionFlagsOffset,
                       &state.SuppressionFlags)) {
        return std::nullopt;
    }
    const auto compatibility = ResolveCompatibilityOverrides(memory, input);
    if (!compatibility.has_value()) {
        return std::nullopt;
    }
    state.NativeResults[static_cast<std::size_t>(contract->Query)] =
        nativeResult != 0U;
    state.CompatibilityOverrides = *compatibility;
    state.Input = input;
    if (observed) *observed = state;
    return ResolveTopScreenItemQuery(contract->Query, state);
}

std::optional<std::uint8_t> ResolveTopScreenDirectSlotItemGuest(
    NativeA32Memory& memory, std::uint8_t slot, std::uint8_t directItem) {
    if (slot != 3U || directItem == 0U) return std::nullopt;
    std::uint8_t enabled = 0xFF, allowedAge = 0;
    std::uint32_t age = 0;
    if (!memory.Read8(0x00588ECAU, &enabled) || enabled == 0xFF)
        return std::nullopt;
    if (directItem > 0x3DU) return directItem;
    if (!memory.Read8(0x00506C58U + directItem, &allowedAge) ||
        !memory.Read32(0x0058795CU, &age)) return std::nullopt;
    if (allowedAge == 9U || allowedAge == age) return directItem;
    return std::nullopt;
}

std::optional<std::uint8_t> ResolveTopScreenSlotItemOverrideGuest(
    NativeA32Memory& memory, std::uint32_t globalContext,
    std::uint8_t slot, const TopScreenExtendedInputFrame& input) {
    const auto compatibility = ResolveCompatibilityOverrides(memory, input);
    if (!compatibility.has_value()) {
        return std::nullopt;
    }
    const bool itemI = slot == 3U && (*compatibility)[0];
    const bool itemII = slot == 4U && (*compatibility)[1];
    if (!itemI && !itemII) {
        return std::nullopt;
    }

    std::uint8_t specialState = 0;
    if (globalContext == 0U ||
        !memory.Read8(globalContext + kSpecialStateOffset, &specialState) ||
        specialState != 0U) {
        return std::nullopt;
    }
    return ResolveTopScreenSlotItemId(
        {slot, 0xFFU, itemI, itemII, false});
}

bool ApplyTopScreenAimProjectileCycleGuest(
    NativeA32Memory& memory, const TopScreenAimProjectileCycleInput& input,
    TopScreenAimProjectileCycleResult* result, std::string* error) {
    if (result == nullptr) {
        SetError(error, "TopScreen aim projectile result is null");
        return false;
    }
    *result = {};

    std::uint32_t aimActive = 0;
    std::uint32_t selectorMode = 0;
    std::uint32_t selector = 0;
    if (!memory.Read32(kAimPromptState + kAimActiveOffset, &aimActive) ||
        !memory.Read32(kAimSelectorState + kAimSelectorModeOffset,
                       &selectorMode) ||
        !memory.Read32(kAimSelectorState + kAimSelectorIndexOffset,
                       &selector)) {
        SetError(error, "cannot read native aiming selector state");
        return false;
    }

    result->InitialSelector = selector;
    result->FinalSelector = selector;
    result->Eligible = aimActive == 1U && selectorMode == 2U;
    if (!result->Eligible || (!input.PreviousPressed && !input.NextPressed)) {
        return true;
    }

    const auto commit = [&](std::uint32_t nextSelector,
                            std::optional<std::uint32_t> prompt) {
        if (!memory.Write32(kAimSelectorState + kAimSelectorIndexOffset,
                            nextSelector)) {
            SetError(error, "cannot write native aiming selector index");
            return false;
        }
        selector = nextSelector;
        result->FinalSelector = selector;
        ++result->Updates;
        if (prompt.has_value()) {
            if (!memory.Write32(kAimPromptState + kAimPromptValueOffset,
                                *prompt)) {
                SetError(error, "cannot write native aiming prompt value");
                return false;
            }
            ++result->PromptWrites;
            result->LastPrompt = *prompt;
        }
        return true;
    };

    if (input.PreviousPressed) {
        if (selector > 0U && selector < 5U) {
            std::uint32_t available = 0;
            if (!memory.Read32(kPreviousAvailabilityTable + selector * 4U,
                               &available)) {
                SetError(error,
                         "cannot read previous aiming selector availability");
                return false;
            }
            if (available != 0U && !commit(selector - 1U, selector + 2U)) {
                return false;
            }
        } else if (selector >= 6U && selector <= 7U &&
                   !commit(selector - 1U, std::nullopt)) {
            return false;
        }
    }

    // The payload reloads the selector after processing the previous edge, so
    // simultaneous edges deliberately observe the first committed result.
    if (input.NextPressed) {
        if (selector < 4U) {
            std::uint32_t available = 0;
            if (!memory.Read32(kNextAvailabilityTable + selector * 4U,
                               &available)) {
                SetError(error,
                         "cannot read next aiming selector availability");
                return false;
            }
            if (available != 0U && !commit(selector + 1U, selector + 4U)) {
                return false;
            }
        } else if (selector >= 5U && selector <= 6U &&
                   !commit(selector + 1U, std::nullopt)) {
            return false;
        }
    }
    return true;
}

bool PrepareTopScreenGameplayDpadGuest(
    NativeA32Memory& memory, const TopScreenGameplayDpadInput& input,
    TopScreenGameplayDpadResult* result, std::string* error) {
    if (result == nullptr) {
        SetError(error, "TopScreen gameplay D-pad result is null");
        return false;
    }
    *result = {};
    if (!input.ViewPressed && !input.OcarinaPressed) {
        return true;
    }

    std::uint32_t scene = 0U;
    if (!memory.Read32(kGameplayRoot + kGameplaySceneOffset, &scene)) {
        SetError(error, "cannot read TopScreen gameplay scene owner");
        return false;
    }
    if (scene == 0U) {
        return true;
    }
    std::uint8_t sceneMode = 0U;
    std::uint8_t sceneVariant = 0U;
    if (!memory.Read8(scene + kSceneModeOffset, &sceneMode) ||
        !memory.Read8(scene + kSceneVariantOffset, &sceneVariant)) {
        SetError(error, "cannot read TopScreen gameplay scene mode");
        return false;
    }
    result->EligibleScene = sceneMode == 3U && sceneVariant == 2U;
    if (!result->EligibleScene) {
        return true;
    }
    if (input.LeftShoulderHeld && input.RightShoulderHeld) {
        result->ShoulderChordSuppressed = true;
        return true;
    }

    if (input.ViewPressed && !input.RightShoulderHeld) {
        std::uint32_t pauseState = 0U;
        std::uint32_t sceneExtension = 0U;
        std::uint32_t specialOwner = 0U;
        if (!memory.Read32(kPauseContext + kPauseStateOffset, &pauseState) ||
            !memory.Read32(scene + kSceneExtensionOffset, &sceneExtension)) {
            SetError(error, "cannot read TopScreen Navi/view state");
            return false;
        }
        if (sceneExtension != 0U &&
            !memory.Read32(sceneExtension + kSceneSpecialOwnerOffset,
                           &specialOwner)) {
            SetError(error, "cannot read TopScreen Navi special owner");
            return false;
        }
        if (pauseState == 2U && specialOwner == 0U) {
            std::uint32_t actionBlock = 0U;
            std::uint32_t naviMode = 0U;
            std::uint32_t pauseFlags = 0U;
            if (!memory.Read32(kGameplayRoot + kGameplayActionBlockOffset,
                               &actionBlock) ||
                !memory.Read32(kPauseContext + kPauseNaviModeOffset,
                               &naviMode) ||
                !memory.Read32(kPauseContext + kPauseFlagsOffset,
                               &pauseFlags)) {
                SetError(error, "cannot read TopScreen Navi/view gates");
                return false;
            }
            if (actionBlock == 0U &&
                (naviMode == 1U ||
                 (naviMode == 0U &&
                  (pauseFlags & 0x10000000U) == 0U))) {
                std::uint16_t naviText = 0U;
                if (!memory.Read16(scene + kSceneNaviTextOffset, &naviText) ||
                    !memory.IsWritable(kPauseContext + kPauseActionOffset,
                                       sizeof(std::uint32_t)) ||
                    !memory.IsWritable(kPauseContext + kPauseStateOffset,
                                       sizeof(std::uint32_t)) ||
                    (naviMode == 1U &&
                     !memory.IsWritable(kNaviModeOneSignal,
                                        sizeof(std::uint32_t)))) {
                    SetError(error,
                             "cannot commit TopScreen Navi/view transition");
                    return false;
                }
                if (!memory.Write32(kPauseContext + kPauseActionOffset,
                                    static_cast<std::uint32_t>(naviText) + 1U) ||
                    (naviMode == 1U &&
                     !memory.Write32(kNaviModeOneSignal, 1U)) ||
                    !memory.Write32(kPauseContext + kPauseStateOffset, 1U)) {
                    SetError(error,
                             "cannot commit TopScreen Navi/view transition");
                    return false;
                }
                result->NaviViewActivated = true;
            }
        }
    }

    if (input.OcarinaPressed) {
        std::uint32_t pauseState = 0U;
        std::uint32_t actionBlock = 0U;
        if (!memory.Read32(kPauseContext + kPauseStateOffset, &pauseState) ||
            !memory.Read32(kGameplayRoot + kGameplayActionBlockOffset,
                           &actionBlock)) {
            SetError(error, "cannot read TopScreen ocarina gates");
            return false;
        }
        result->OcarinaQueryRequired =
            pauseState == 2U && actionBlock == 0U;
    }
    return true;
}

bool CommitTopScreenGameplayOcarinaGuest(
    NativeA32Memory& memory, std::uint32_t nativeEligibility,
    TopScreenGameplayDpadResult* result, std::string* error) {
    if (result == nullptr) {
        SetError(error, "TopScreen gameplay D-pad result is null");
        return false;
    }
    if (!result->OcarinaQueryRequired || nativeEligibility != 1U) {
        return true;
    }
    if (!memory.IsWritable(kPauseContext + kPauseOcarinaOffset,
                           sizeof(std::uint32_t)) ||
        !memory.IsWritable(kPauseContext + kPauseStateOffset,
                           sizeof(std::uint32_t)) ||
        !memory.Write32(kPauseContext + kPauseOcarinaOffset, 1U) ||
        !memory.Write32(kPauseContext + kPauseStateOffset, 1U)) {
        SetError(error, "cannot commit TopScreen ocarina transition");
        return false;
    }
    result->OcarinaActivated = true;
    return true;
}

} // namespace Oot3dNativeGame
