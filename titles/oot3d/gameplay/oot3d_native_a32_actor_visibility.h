#pragma once

// Applies the actor visibility policy to the EUR title's actor culling and
// distance checks at existing A32 block entries. Gameplay state is changed
// only where the title itself decides visibility or spawn distance.

#include "oot3d_actor_visibility_policy.h"
#include "fast/oot3d/scene_presentation_policy.h"
#include "recomp/a32_runtime.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {

class NativeA32Memory;

// Actor_DrawAll (0x00461904, r0 = play, r1 = actor context) interleaves
// culling and drawing across every actor list. Its
// result block: r4 = actor, r0 = native result (0 culled), r1 = the previous
// culling state, r7 = play + 0x2000; actor+0x120 already holds the new state.
inline constexpr uint32_t kActorCullingEntry = 0x00461904U;
inline constexpr uint32_t kActorCullingResultHook = 0x00461AC4U;
// Obj_Mure spawn and active actions, the only readers of its distance table.
inline constexpr uint32_t kObjMureSpawnEntry = 0x003BC5DCU;
inline constexpr uint32_t kObjMureActiveEntry = 0x003CC298U;
// Obj_Mure2 spawn and active actions: r0 = actor, r1 = play; the update has
// just stored the native distance scalar (1, or 4 in cutscenes) at +0x1DC.
inline constexpr uint32_t kObjMure2SpawnEntry = 0x003B30ACU;
inline constexpr uint32_t kObjMure2ActiveEntry = 0x003AEF48U;
// En_Wood02 offspring: r0 = the main spawn-zone test, r4 = the spawner, whose
// +0xEC..+0xF4 hold the candidate's projection and +0xF8 its inverse W;
// r11 = play + 0x2000.
inline constexpr uint32_t kEnWood02SpawnZoneResult = 0x0034E854U;
// En_Ko fade branches: s0 = the distance compared with the appear distance.
inline constexpr uint32_t kEnKoFadeDistanceA = 0x001B5FE0U;
inline constexpr uint32_t kEnKoFadeDistanceB = 0x001B60E8U;
// Actor_UpdateAlphaByDistance (0x00341C28), Mido's and Saria's fade, after its
// distance: r4 = actor, s0 = distance, s16 = radius. Mido's house and Saria's
// cutscene action set full alpha without reaching it.
inline constexpr uint32_t kAlphaByDistanceCompare = 0x00341CD8U;

// All are existing emitted block entries, including the post-reset boundary.
inline constexpr uint32_t kActorLightAllocate = 0x0035BFB4U;
inline constexpr uint32_t kActorLightEpochReset = 0x0041633CU;
inline constexpr uint32_t kActorDrawEntry = 0x002D5F68U;
inline constexpr uint32_t kActorCullingNext = 0x00461BBCU;
inline constexpr uint32_t kActorDeferredDrawReturn = 0x00461D2CU;
inline constexpr uint32_t kActorCullingEnd = 0x00461EC8U;

struct NativeActorVisibilityReservation {
    uint32_t Actor = 0U;
    uint32_t Draw = 0U;
    uint16_t Id = 0U;
    uint32_t LightCost = 1U;
    bool Reserved = true;
    bool Selected = false;
    bool Extended = false;
    bool Restore = false;
    bool NativeDraw = false;
    uint8_t NativeState = 0U;
    uint8_t NativeFade = 0U;
    uint8_t ExtendedFade = 0U;
};

struct NativeActorVisibilityRetention {
    uint64_t Identity = 0U;
    uint8_t Fade = 0U;
};

struct NativeActorVisibilityState {
    // Snapshot of the settings and presentation, taken between host frames.
    bool ExtendedDrawDistance = false;
    // The presented view: the frustum hook widens every perspective the
    // GPU draws with, but the title builds its culling matrix separately.
    Fast::Oot3d::ScenePresentationPolicy Presentation{};
    float FovMultiplier = 1.0F;
    // The presented volume of the current culling walk, from its play state.
    ActorCullingVolume CullingVolume{};
    bool CullingVolumeValid = false;
    // Host-owned writes to the title's read-only distance constants.
    NativeA32Memory* HostMemory = nullptr;
    // The overflow region could not be mapped: the native arena is the limit.
    bool LightOverflowUnavailable = false;
    bool Diagnostics = false;
    bool ReportedUnknownConstants = false;
    uint64_t NativeVisible = 0U;
    uint64_t NativeCulled = 0U;
    uint64_t Extended = 0U;
    // Observational counts only; admission always reads the live arena. A
    // savestate load/first partial epoch cannot lend stale credits.
    uint64_t LightEpoch = 0U;
    uint32_t LightAllocations = 0U;
    uint32_t PreviousLightAllocations = 0U;
    uint32_t PeakLightAllocations = 0U;
    // Arena cursor moves into the overflow region (and wraps inside it).
    uint64_t LightOverflowRelocations = 0U;
    uint64_t LightOverflowWraps = 0U;
    uint64_t BudgetLimitedWalks = 0U;
    uint64_t BudgetDenied = 0U;
    uint64_t BudgetInvalidWalks = 0U;
    uint32_t WalkDenied = 0U;
    uint32_t RemainingLights = 0U;
    uint32_t Arena = 0U;
    uint32_t DeferredCountBefore = 0U;
    uint32_t DeferredActor = 0U;
    bool BudgetWalk = false;
    bool BudgetValid = false;
    std::vector<NativeActorVisibilityReservation> Reservations;
    std::vector<NativeActorVisibilityRetention> Retained;
};

// A standalone save must wait until temporary guest culling fields have
// been restored. Successful loads discard outgoing host state, without
// restoring it into the incoming guest memory; settings/pointers survive.
[[nodiscard]] bool NativeActorVisibilityHasPendingRestore(const NativeActorVisibilityState& visibility) noexcept;
void ResetNativeActorVisibilityAfterStateLoad(NativeActorVisibilityState& visibility) noexcept;

[[nodiscard]] std::span<const uint32_t> NativeActorVisibilityHookPcs() noexcept;

// Returns true when the pc belongs to this policy.
bool ApplyNativeActorVisibilityHook(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    NativeActorVisibilityState& visibility);

} // namespace Oot3dNativeGame
