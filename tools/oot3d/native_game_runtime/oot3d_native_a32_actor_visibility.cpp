#include "oot3d_native_a32_actor_visibility.h"

#include "fast/oot3d/perspective_fov_policy.h"
#include "oot3d_native_a32_memory.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>

namespace Oot3dNativeGame {
namespace {

using oot3d::recomp::a32::GuestState;
using oot3d::recomp::a32::MemoryBus;

constexpr uint32_t kActorProjectedPosition = 0xECU;
constexpr uint32_t kActorProjectedW = 0xF8U;
constexpr uint32_t kActorUncullForward = 0xFCU;
constexpr uint32_t kActorUncullScale = 0x100U;
constexpr uint32_t kActorUncullDownward = 0x104U;
constexpr uint32_t kActorCullingState = 0x120U;
constexpr uint32_t kObjMure2DistanceScalar = 0x1DCU;
// En_Viewer hides behind culling during cutscene camera pans (SoH skips it).
constexpr uint16_t kActorEnViewer = 0x002AU; // ACTOR_EN_VIEWER, actor_object_semantics.h
// The Kokiri SoH stops fading: En_Ko has its own fade, En_Md (Mido) and
// En_Sa (Saria) share Actor_UpdateAlphaByDistance.
constexpr uint16_t kActorEnSa = 0x0146U;
constexpr uint16_t kActorEnMd = 0x016DU;

// The view-projection the title culls actors with (play+0x5BB4, row-major).
constexpr uint32_t kPlayCullingViewProjection = 0x5BB4U;

// Obj_Mure's spawn distances (sZClip: 1600, 1600, 1000, 1000, 1000) and the
// 40 its retirement adds, each read from guest memory at every use. SoH
// scales both the spawn and the retirement distance by 5.
constexpr uint32_t kObjMureDistanceTable = 0x005354B8U;
constexpr uint32_t kObjMureRetireMargin = 0x003CC430U;
constexpr std::array kObjMureNativeDistances{1600.0F, 1600.0F, 1000.0F, 1000.0F, 1000.0F, 40.0F};

// Both the culling walk and En_Wood02 keep play + 0x2000 in a register.
constexpr uint32_t kPlayHighHalf = 0x2000U;

constexpr std::array kHookPcs{
    kEnKoFadeDistanceA, kEnKoFadeDistanceB, kActorDrawEntry, kAlphaByDistanceCompare,
    kEnWood02SpawnZoneResult, kActorLightAllocate, kObjMure2ActiveEntry, kObjMure2SpawnEntry,
    kObjMureSpawnEntry, kObjMureActiveEntry, kActorLightEpochReset, kActorCullingEntry,
    kActorCullingResultHook, kActorCullingNext, kActorDeferredDrawReturn, kActorCullingEnd,
};

bool ReadFloat(MemoryBus& memory, uint32_t address, float& value) {
    uint32_t bits = 0U;
    if (!memory.Read32(address, &bits)) return false;
    value = std::bit_cast<float>(bits);
    return true;
}

bool ReadCullingSample(MemoryBus& memory, uint32_t actor, ActorCullingSample& sample) {
    return ReadFloat(memory, actor + kActorProjectedPosition + 0U, sample.X) &&
           ReadFloat(memory, actor + kActorProjectedPosition + 4U, sample.Y) &&
           ReadFloat(memory, actor + kActorProjectedPosition + 8U, sample.Z) &&
           ReadFloat(memory, actor + kActorProjectedW, sample.W) &&
           ReadFloat(memory, actor + kActorUncullForward, sample.Forward) &&
           ReadFloat(memory, actor + kActorUncullScale, sample.Scale) &&
           ReadFloat(memory, actor + kActorUncullDownward, sample.Downward);
}

// How much wider and taller the presented view is than the culling
// projection. The x and y rows of the culling view-projection scale by the
// inverse half-extent tangents (their spatial norms, as the view is rigid);
// the presented view is that frustum after ResolvePerspectiveFov, as the
// frustum hook applies it to the rendered camera.
ActorCullingVolume PresentedVolume(MemoryBus& memory, uint32_t play,
                                   const NativeActorVisibilityState& visibility) {
    ActorCullingVolume volume;
    volume.DistanceMultiplier =
        visibility.ExtendedDrawDistance ? kExtendedActorDistanceMultiplier : 1.0F;
    if (play == 0U) return volume;
    std::array<float, 8> rows{};
    for (uint32_t i = 0; i < rows.size(); ++i)
        if (!ReadFloat(memory, play + kPlayCullingViewProjection + i * 4U, rows[i])) return volume;
    const float scaleX = std::sqrt(rows[0] * rows[0] + rows[1] * rows[1] + rows[2] * rows[2]);
    const float scaleY = std::sqrt(rows[4] * rows[4] + rows[5] * rows[5] + rows[6] * rows[6]);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 1.0e-6F || scaleY <= 1.0e-6F)
        return volume;
    const auto resolved = Fast::Oot3d::ResolvePerspectiveFov({
        {-1.0F / scaleX, 1.0F / scaleX, -1.0F / scaleY, 1.0F / scaleY, 1.0F, 2.0F},
        visibility.FovMultiplier,
        visibility.Presentation,
        true,
    });
    volume.Horizontal = resolved.HorizontalScale;
    volume.Vertical = resolved.VerticalScale;
    return volume;
}

bool Extends(const ActorCullingVolume& volume) {
    return volume.DistanceMultiplier > 1.0F || volume.Horizontal > 1.0F || volume.Vertical > 1.0F;
}


// The native arena is Graph_ThreadEntry's scratch buffer. Validate the live
// bounds on every admission; an epoch counter is never used as free space.
bool ReadLightUsage(MemoryBus& memory, uint32_t arena, uint32_t& used) {
    uint32_t size = 0, base = 0, head = 0, tail = 0;
    if (arena == 0U || !memory.Read32(arena, &size) ||
        !memory.Read32(arena + 4U, &base) || !memory.Read32(arena + 8U, &head) ||
        !memory.Read32(arena + 12U, &tail) || size != 0x4000U ||
        base != 0x005656A0U || head != base || tail < base || tail > base + size ||
        ((base + size - tail) % 0x80U) != 0U) return false;
    used = (base + size - tail) / 0x80U;
    return true;
}

uint64_t ActorIdentity(uint32_t actor, uint16_t id) {
    return (static_cast<uint64_t>(actor) << 16U) | id;
}

NativeActorVisibilityReservation* Reservation(NativeActorVisibilityState& v, uint32_t actor) {
    for (auto& entry : v.Reservations)
        if (entry.Actor == actor) return &entry;
    return nullptr;
}

void ReleaseReservation(NativeActorVisibilityState& v, NativeActorVisibilityReservation& entry) {
    if (!entry.Reserved) return;
    entry.Reserved = false;
    if (v.RemainingLights >= entry.LightCost) v.RemainingLights -= entry.LightCost;
    else v.BudgetValid = false;
}

void RestoreNativeCulling(MemoryBus& memory, NativeActorVisibilityReservation& entry) {
    if (!entry.Restore) return;
    uint32_t flags = 0U;
    (void)memory.Read8(entry.Actor + 0x19EU, &entry.ExtendedFade);
    if (memory.Read32(entry.Actor + 4U, &flags)) {
        flags = (flags & ~0x40U) | (entry.NativeFade != 0U ? 0x40U : 0U);
        (void)memory.Write32(entry.Actor + 4U, flags);
        (void)memory.Write8(entry.Actor + kActorCullingState, entry.NativeState);
        (void)memory.Write8(entry.Actor + 0x19EU, entry.NativeFade);
        if (!entry.NativeDraw) (void)memory.Write8(entry.Actor + 0x121U, 0U);
    }
    entry.Restore = false;
}

// These draw callbacks can allocate additional records (Fishing also loops
// over pond props/effects). Leave their whole walk native instead of guessing
// their costs. The helper's update/init callers precede this walk and are
// already included by the live cursor. Player's one get-item record is
// reserved per actor alongside its wrapper.
bool HasUnbudgetedDraw(uint32_t draw) {
    return draw == 0x00135384U || draw == 0x00228A34U || draw == 0x00278B20U || draw == 0x0037A590U ||
           draw == 0x00382B1CU || draw == 0x003B5290U;
}

// Projection is for PRIORITY only. Safety never depends on predicting a
// native culling result or on a previous frame's projected coordinates.
bool PrioritySample(MemoryBus& memory, uint32_t actor, const std::array<float, 16>& matrix,
                    ActorCullingSample& sample) {
    std::array<float, 3> world{};
    for (uint32_t i = 0; i < world.size(); ++i)
        if (!ReadFloat(memory, actor + 0x28U + i * 4U, world[i])) return false;
    std::array<float, 4> projected{};
    for (size_t row = 0; row < projected.size(); ++row)
        projected[row] = matrix[row * 4] * world[0] + matrix[row * 4 + 1] * world[1] +
                         matrix[row * 4 + 2] * world[2] + matrix[row * 4 + 3];
    sample.X = projected[0]; sample.Y = projected[1];
    sample.Z = -projected[2]; sample.W = projected[3];
    return ReadFloat(memory, actor + kActorUncullForward, sample.Forward) &&
           ReadFloat(memory, actor + kActorUncullScale, sample.Scale) &&
           ReadFloat(memory, actor + kActorUncullDownward, sample.Downward);
}

void BeginBudgetWalk(GuestState& state, MemoryBus& memory, NativeActorVisibilityState& v) {
    v.BudgetWalk = v.ExtendedDrawDistance;
    v.BudgetValid = false;
    v.Reservations.clear();
    v.RemainingLights = 0U;
    v.WalkDenied = 0U;
    v.DeferredActor = 0U;
    if (!v.BudgetWalk) { v.Retained.clear(); return; }
    // This reservation contract belongs to the normal Gameplay_Draw pass.
    // An independent invocation must not borrow its singleton reservations.
    if (state.r[14] != 0x002E2CE0U || state.r[1] != state.r[0] + 0x208CU) return;
    uint32_t used = 0U;
    if (!memory.Read32(state.r[0], &v.Arena) || !ReadLightUsage(memory, v.Arena, used)) return;
    v.LightAllocations = used;
    v.PeakLightAllocations = std::max(v.PeakLightAllocations, used);
    std::array<float, 16> matrix{};
    for (uint32_t i = 0; i < matrix.size(); ++i)
        if (!ReadFloat(memory, state.r[0] + kPlayCullingViewProjection + i * 4U, matrix[i])) return;
    std::vector<ActorExtensionCandidate> candidates;
    // ActorContext+0x0C: twelve {count, head} lists; actor+0x130: next.
    // One reservation for EVERY actor, even a null draw, init, no-light flag,
    // or native reject. This bounds one Actor_Draw wrapper without forecasting
    // which actors will become visible later in the same walk. Player costs
    // two, to include its optional get-item draw.
    for (uint32_t category = 0; category < 12U; ++category) {
        uint32_t actor = 0U;
        if (!memory.Read32(state.r[1] + 0x10U + category * 8U, &actor)) return;
        while (actor != 0U) {
            if (v.Reservations.size() >= 1024U || actor > 0xFFFFFE00U || Reservation(v, actor)) return;
            NativeActorVisibilityReservation entry;
            entry.Actor = actor;
            uint32_t next = 0U, init = 0U;
            if (!memory.Read16(actor, &entry.Id) || !memory.Read32(actor + 0x140U, &entry.Draw) ||
                !memory.Read32(actor + 0x134U, &init) || !memory.Read32(actor + 0x130U, &next) ||
                HasUnbudgetedDraw(entry.Draw)) return;
            // Player_Draw can tail-call Player_DrawGetItem once. Reserve
            // it per instance, including unusual duplicate-player states.
            entry.LightCost = entry.Id == 0U || entry.Draw == 0x004BF618U ? 2U : 1U;
            const auto identity = ActorIdentity(actor, entry.Id);
            const auto retained = std::find_if(v.Retained.begin(), v.Retained.end(),
                [identity](const auto& prior) { return prior.Identity == identity; });
            if (retained != v.Retained.end()) entry.ExtendedFade = retained->Fade;
            v.Reservations.push_back(entry);
            v.RemainingLights += entry.LightCost;
            ActorCullingSample sample;
            if (init == 0U && entry.Draw != 0U && entry.Id != kActorEnViewer &&
                PrioritySample(memory, actor, matrix, sample) &&
                !ActorInsideNativeCullingVolume(sample) &&
                ActorInsideExtendedCullingVolume(sample, v.CullingVolume)) {
                candidates.push_back({identity, sample.X * sample.X + sample.Y * sample.Y + sample.Z * sample.Z,
                    retained != v.Retained.end(), false});
            }
            actor = next;
        }
    }
    SelectActorExtensions(candidates);
    for (const auto& candidate : candidates)
        if (candidate.Selected)
            if (auto* entry = Reservation(v, static_cast<uint32_t>(candidate.Identity >> 16U)))
                entry->Selected = true;
    v.BudgetValid = true;
}

void EndBudgetWalk(MemoryBus& memory, NativeActorVisibilityState& v) {
    if (!v.BudgetWalk) return;
    v.Retained.clear();
    for (auto& entry : v.Reservations) {
        RestoreNativeCulling(memory, entry); // also covers deferred draws skipped by the title
        if (entry.Extended) v.Retained.push_back({ActorIdentity(entry.Actor, entry.Id), entry.ExtendedFade});
    }
    if (!v.BudgetValid) ++v.BudgetInvalidWalks;
    if (v.WalkDenied != 0U || !v.BudgetValid) {
        const auto count = ++v.BudgetLimitedWalks;
        if ((count & (count - 1U)) == 0U)
            std::fprintf(stderr, "TRIAEVUM_ACTOR_VISIBILITY_BUDGET walks=%llu epoch=%llu denied=%u "
                         "valid=%u lights=%u previous=%u peak=%u budget=%u reserve=%u retained=%zu\n",
                         static_cast<unsigned long long>(count), static_cast<unsigned long long>(v.LightEpoch),
                         v.WalkDenied, v.BudgetValid ? 1U : 0U, v.LightAllocations,
                         v.PreviousLightAllocations, v.PeakLightAllocations, kActorLightBudget,
                         kActorLightAuxiliaryReserve, v.Retained.size());
    }
    v.BudgetWalk = false;
    v.BudgetValid = false;
    v.RemainingLights = 0U;
}

bool ApplyBudgetedCulling(GuestState& state, MemoryBus& memory, NativeActorVisibilityState& v,
                         uint8_t nativeState) {
    auto* entry = Reservation(v, state.r[4]);
    uint32_t used = 0U;
    if (!v.BudgetWalk || !v.BudgetValid || !entry || !entry->Reserved || !entry->Selected ||
        !ReadLightUsage(memory, v.Arena, used) || !ActorLightBudgetAllows(used, v.RemainingLights)) {
        ++v.BudgetDenied;
        ++v.WalkDenied;
        return false;
    }
    uint8_t fade = 0U;
    uint32_t flags = 0U, init = 0U, draw = 0U;
    if (!memory.Read8(entry->Actor + 0x19EU, &fade) || !memory.Read32(entry->Actor + 4U, &flags) ||
        !memory.Read32(entry->Actor + 0x134U, &init) || !memory.Read32(entry->Actor + 0x140U, &draw) ||
        draw != entry->Draw || HasUnbudgetedDraw(draw)) {
        v.BudgetValid = false;
        ++v.BudgetDenied;
        ++v.WalkDenied;
        return false;
    }
    entry->NativeState = nativeState;
    entry->NativeFade = NativeCulledActorFade(static_cast<ActorCullingClass>(nativeState), fade);
    entry->NativeDraw = init == 0U && draw != 0U && ((flags & 0x20U) != 0U || entry->NativeFade != 0U);
    if (!memory.Write8(entry->Actor + kActorCullingState, 0U)) return false;
    const uint8_t beforeFade = std::max(entry->ExtendedFade,
        static_cast<uint8_t>(entry->NativeFade != 0U ? entry->NativeFade - 1U : 0U));
    (void)memory.Write8(entry->Actor + 0x19EU, beforeFade);
    entry->Restore = true;
    entry->Extended = true;
    // Let the title ramp the extension's separate host-owned alpha. Restore
    // native history after drawing so grants cannot create unbudgeted
    // ten-frame fade liabilities during a burst of new native visibility.
    state.r[1] = static_cast<uint32_t>(ActorCullingClass::Far);
    state.r[0] = 1U;
    ++v.Extended;
    return true;
}

void ApplyActorCulling(GuestState& state, MemoryBus& memory, NativeActorVisibilityState& visibility) {
    const uint32_t actor = state.r[4];
    visibility.DeferredCountBefore = state.r[5];
    if (state.r[0] != 0U) {
        ++visibility.NativeVisible;
        return;
    }
    ++visibility.NativeCulled;
    const auto volume = visibility.CullingVolumeValid
        ? visibility.CullingVolume
        : PresentedVolume(memory, state.r[7] - kPlayHighHalf, visibility);
    if (visibility.Diagnostics && visibility.NativeCulled % 4096U == 1U)
        std::fprintf(stderr, "[actor-visibility] presented=%.4f,%.4f visible=%llu culled=%llu extended=%llu budget_denied=%llu limited_walks=%llu invalid_walks=%llu lights=%u previous=%u peak=%u\n",
                     volume.Horizontal, volume.Vertical,
                     static_cast<unsigned long long>(visibility.NativeVisible),
                     static_cast<unsigned long long>(visibility.NativeCulled),
                     static_cast<unsigned long long>(visibility.Extended),
                     static_cast<unsigned long long>(visibility.BudgetDenied),
                     static_cast<unsigned long long>(visibility.BudgetLimitedWalks),
                     static_cast<unsigned long long>(visibility.BudgetInvalidWalks),
                     visibility.LightAllocations, visibility.PreviousLightAllocations, visibility.PeakLightAllocations);
    if (!Extends(volume)) return;
    uint16_t id = 0U;
    ActorCullingSample sample;
    uint8_t nativeState = 0U;
    if (!memory.Read16(actor, &id) || id == kActorEnViewer || !ReadCullingSample(memory, actor, sample) ||
        !memory.Read8(actor + kActorCullingState, &nativeState))
        return;
    const auto classified = ClassifyExtendedActorCulling(sample, volume);
    if (visibility.ExtendedDrawDistance) {
        if (classified == ActorCullingClass::Inside)
            (void)ApplyBudgetedCulling(state, memory, visibility, nativeState);
        return;
    }
    const auto next = static_cast<uint8_t>(classified);
    // A pass is the title's own: state 0 and a true result. The fade that
    // follows ramps in from state 2 and fills at once from state 1. A
    // failure inside the extended depth is no longer a far one.
    if (next != nativeState && !memory.Write8(actor + kActorCullingState, next)) return;
    if (classified == ActorCullingClass::Inside) {
        state.r[0] = 1U;
        ++visibility.Extended;
    }
}

// Restore native Obj_Mure population bounds before either reader uses them.
// Accept the old policy's 5x values, including states saved by that policy.
void AssertObjMureDistances(NativeActorVisibilityState& visibility) {
    if (visibility.HostMemory == nullptr) return;
    auto& memory = *visibility.HostMemory;
    // Spawned groups persist beyond this frame and may bypass culling. Keep
    // native population bounds; undo a known old extended value on restore.
    const float scale = 1.0F;
    const auto address = [](size_t i) {
        return i + 1U < kObjMureNativeDistances.size()
            ? kObjMureDistanceTable + static_cast<uint32_t>(i) * 4U
            : kObjMureRetireMargin;
    };
    for (size_t i = 0; i < kObjMureNativeDistances.size(); ++i) {
        uint32_t bits = 0U;
        const float native = kObjMureNativeDistances[i];
        const auto wanted = std::bit_cast<uint32_t>(native * scale);
        if (!memory.Read32(address(i), &bits)) return;
        if (bits == wanted) continue;
        if (bits != std::bit_cast<uint32_t>(native) &&
            bits != std::bit_cast<uint32_t>(native * kExtendedActorDistanceMultiplier)) {
            if (!visibility.ReportedUnknownConstants)
                std::fprintf(stderr, "[actor-visibility] Obj_Mure distance %08x at %08x is not the EUR value\n",
                             bits, address(i));
            visibility.ReportedUnknownConstants = true;
            return;
        }
        (void)memory.WriteHost<uint32_t>(address(i), wanted);
    }
}

void ApplyObjMure2(GuestState& state, MemoryBus& memory, const NativeActorVisibilityState& visibility) {
    const uint32_t actor = state.r[0];
    float scalar = 0.0F;
    float x = 0.0F;
    float z = 0.0F;
    if (!ReadFloat(memory, actor + kObjMure2DistanceScalar, scalar) ||
        !ReadFloat(memory, actor + kActorProjectedPosition + 0U, x) ||
        !ReadFloat(memory, actor + kActorProjectedPosition + 8U, z))
        return;
    // The update stores 1 or 4 before every action; anything else is ours.
    if (scalar != 1.0F && scalar != 4.0F) return;
    const auto volume = PresentedVolume(memory, state.r[1], visibility);
    const float adjusted =
        ObjMure2DistanceScalar(scalar, x, z, volume.Horizontal, false);
    if (adjusted != scalar && std::isfinite(adjusted))
        (void)memory.Write32(actor + kObjMure2DistanceScalar, std::bit_cast<uint32_t>(adjusted));
}

} // namespace

bool NativeActorVisibilityHasPendingRestore(const NativeActorVisibilityState& visibility) noexcept {
    return std::any_of(visibility.Reservations.begin(), visibility.Reservations.end(),
                       [](const auto& entry) { return entry.Restore; });
}

void ResetNativeActorVisibilityAfterStateLoad(NativeActorVisibilityState& visibility) noexcept {
    visibility.CullingVolumeValid = false;
    visibility.LightEpoch = 0U;
    visibility.LightAllocations = 0U;
    visibility.PreviousLightAllocations = 0U;
    visibility.WalkDenied = 0U;
    visibility.RemainingLights = 0U;
    visibility.Arena = 0U;
    visibility.DeferredCountBefore = 0U;
    visibility.DeferredActor = 0U;
    visibility.BudgetWalk = false;
    visibility.BudgetValid = false;
    visibility.Reservations.clear();
    visibility.Retained.clear();
}

std::span<const uint32_t> NativeActorVisibilityHookPcs() noexcept {
    return kHookPcs;
}

bool ApplyNativeActorVisibilityHook(
    uint32_t pc, GuestState& state, MemoryBus& memory,
    NativeActorVisibilityState& visibility) {
    switch (pc) {
    case kActorLightEpochReset:
        visibility.PreviousLightAllocations = visibility.LightAllocations;
        visibility.LightAllocations = 0U;
        ++visibility.LightEpoch;
        return true;
    case kActorLightAllocate: {
        uint32_t used = 0U;
        if (ReadLightUsage(memory, state.r[1], used)) {
            visibility.LightAllocations = used + 1U;
            visibility.PeakLightAllocations = std::max(visibility.PeakLightAllocations, used + 1U);
        }
        return true;
    }
    case kActorDrawEntry:
        if (visibility.BudgetWalk) {
            if (auto* entry = Reservation(visibility, state.r[1])) ReleaseReservation(visibility, *entry);
            else visibility.BudgetValid = false;
            if (state.r[14] == kActorDeferredDrawReturn) visibility.DeferredActor = state.r[1];
        }
        return true;
    case kActorCullingNext:
        if (visibility.BudgetWalk) {
            if (auto* entry = Reservation(visibility, state.r[4])) {
                if (state.r[5] == visibility.DeferredCountBefore) {
                    ReleaseReservation(visibility, *entry);
                    RestoreNativeCulling(memory, *entry);
                }
                // Otherwise the title enqueued this actor for a lens draw:
                // retain both its reservation and temporary culling state.
            } else visibility.BudgetValid = false;
        }
        return true;
    case kActorDeferredDrawReturn:
        if (visibility.BudgetWalk)
            if (auto* entry = Reservation(visibility, visibility.DeferredActor))
                RestoreNativeCulling(memory, *entry);
        visibility.DeferredActor = 0U;
        return true;
    case kActorCullingEnd:
        EndBudgetWalk(memory, visibility);
        return true;
    case kActorCullingEntry:
        // The culling matrix is fixed for the walk: resolve its view once.
        visibility.CullingVolume = PresentedVolume(memory, state.r[0], visibility);
        visibility.CullingVolumeValid = true;
        BeginBudgetWalk(state, memory, visibility);
        return true;
    case kActorCullingResultHook:
        ApplyActorCulling(state, memory, visibility);
        return true;
    case kObjMureSpawnEntry:
    case kObjMureActiveEntry:
        AssertObjMureDistances(visibility);
        return true;
    case kObjMure2SpawnEntry:
    case kObjMure2ActiveEntry:
        ApplyObjMure2(state, memory, visibility);
        return true;
    case kEnWood02SpawnZoneResult:
        // Off was already a no-op. Group offspring remain at native range.
        return true;
    case kEnKoFadeDistanceA:
    case kEnKoFadeDistanceB:
        // Distance 0 is inside any appear distance: the Kokiri fade in with
        // the title's own smoothing and stay.
        if (visibility.ExtendedDrawDistance) state.vfp[0] = 0U;
        return true;
    case kAlphaByDistanceCompare: {
        // Distance 0 takes the near branch: the title raises the alpha to
        // 255 with its own smoothing.
        uint16_t id = 0U;
        if (visibility.ExtendedDrawDistance && memory.Read16(state.r[4], &id) &&
            (id == kActorEnMd || id == kActorEnSa))
            state.vfp[0] = 0U;
        return true;
    }
    default:
        return false;
    }
}

} // namespace Oot3dNativeGame
