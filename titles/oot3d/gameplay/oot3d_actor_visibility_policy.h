#pragma once

// Title-owned actor visibility policy: SoH's extended actor draw distance and
// a culling volume that matches the presented (wider) view. The functions
// here are pure; oot3d_native_a32_actor_visibility applies them to the guest.

#include <cstdint>
#include <span>

namespace Oot3dNativeGame {

// What the native culling test (0x00461904) reads from an actor: the
// projected position and W at +0xEC..+0xF8 and the uncull zone at +0xFC.
struct ActorCullingSample {
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;
    float W = 0.0F;
    float Forward = 0.0F;
    float Scale = 0.0F;
    float Downward = 0.0F;
};

struct ActorCullingVolume {
    // Forward range multiplier (SoH's "Increase Actor Draw Distance").
    float DistanceMultiplier = 1.0F;
    // Presented extent over the extent the culling projection covers.
    float Horizontal = 1.0F;
    float Vertical = 1.0F;
};

// The title's culling states (actor+0x120): inside, beyond the forward range
// (fades out), or behind the camera or outside the sides (cut at once).
enum class ActorCullingClass : uint8_t { Inside = 0, Outside = 1, Far = 2 };

// The native volume: the actor is visible within -scale < z < forward + scale
// and inside the projected [-1, 1] square grown by its scale; the far test
// comes first, as in the title.
[[nodiscard]] ActorCullingClass ClassifyNativeActorCulling(
    const ActorCullingSample& sample) noexcept;

// The extended volume. Forward ranges under 500 gain 1000 before the
// multiplier, as SoH does, so short-range actors also reach farther; this
// applies only with a multiplier above 1. The lateral and vertical bounds
// grow by the presentation factors. Never smaller than the native volume.
[[nodiscard]] ActorCullingClass ClassifyExtendedActorCulling(
    const ActorCullingSample& sample,
    const ActorCullingVolume& volume) noexcept;

[[nodiscard]] inline bool ActorInsideNativeCullingVolume(
    const ActorCullingSample& sample) noexcept {
    return ClassifyNativeActorCulling(sample) == ActorCullingClass::Inside;
}

[[nodiscard]] inline bool ActorInsideExtendedCullingVolume(
    const ActorCullingSample& sample,
    const ActorCullingVolume& volume) noexcept {
    return ClassifyExtendedActorCulling(sample, volume) == ActorCullingClass::Inside;
}

// Obj_Mure2 spawns and retires its group when X^2 + Z^2 of its projected
// position is under a table distance times a scalar (1, or 4 in cutscenes).
// Returns the scalar that makes that test see the presented width (X / H)
// and, extended, at least SoH's 5x distance (25 on squared distances).
[[nodiscard]] float ObjMure2DistanceScalar(float nativeScalar, float projectedX,
                                           float projectedZ, float horizontal,
                                           bool extended) noexcept;

// 0x4000 / 0x80 records. Admission targets 120, leaving eight records of
// physical headroom. Four of the 120 are also left unassigned: two cover
// the singleton gameplay/weather records even when already measured, and
// two are extra headroom. Per-actor auxiliary draws need their own reserve.
inline constexpr uint32_t kActorLightCapacity = 128U;
inline constexpr uint32_t kActorLightBudget = 120U;
inline constexpr uint32_t kActorLightAuxiliaryReserve = 4U;
// SingleScreen 3.0 never denies a lit actor: past the arena its records come
// from an overflow pool (0x005D11A0). TriAevum's pool is a separate guest
// region, so overflow records never alias live arena records (3.0 reuses
// the arena's own first 64). Admission counts it as extra capacity.
inline constexpr uint32_t kActorLightOverflowBase = 0x06100000U;
inline constexpr uint32_t kActorLightOverflowSize = 0x40000U;
inline constexpr uint32_t kActorLightOverflowRecords = kActorLightOverflowSize / 0x80U;
// A visual policy ceiling, NOT an estimate of native light demand. The
// overflow pool lifted the light bound; this ceiling stays a visual choice.
inline constexpr uint32_t kActorExtensionLimit = 32U;

struct ActorExtensionCandidate {
    uint64_t Identity = 0U;
    float DistanceSquared = 0.0F;
    bool Retained = false;
    bool Selected = false;
};

// Stable incumbents first, then distance, then identity; input order is kept.
void SelectActorExtensions(std::span<ActorExtensionCandidate> candidates);

// remaining includes the current actor and every not-yet-resolved actor,
// including deferred draws. used is measured in the CURRENT arena epoch.
// overflow: the cursor can move into the overflow region; without it the
// native arena is the whole capacity.
[[nodiscard]] bool ActorLightBudgetAllows(uint32_t used, uint32_t remaining,
                                          bool overflow) noexcept;

// Native false-result fade branch (Actor_DrawAll): Far decrements; Outside
// clears. Used to restore native history after an extension-only draw.
[[nodiscard]] uint8_t NativeCulledActorFade(ActorCullingClass classification,
                                         uint8_t fade) noexcept;

inline constexpr float kExtendedActorDistanceMultiplier = 5.0F;

} // namespace Oot3dNativeGame
