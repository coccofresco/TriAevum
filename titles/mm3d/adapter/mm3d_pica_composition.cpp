#include "mm3d_pica_composition.h"

#include "mm3d_title_contracts.h"
#include "mm3d_scene_environment.h"
#include "mm3d_lighting_hook_sites.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

namespace Oot3dNativeGame {
namespace {

struct ScopeDescriptor {
    uint32_t EntryPc;
    uint32_t ReturnPc;
    Oot3dPicaCompositionLayer Layer;
    Oot3dPicaCompositionProvenance Provenance;
};

constexpr std::array kScopes{
    // Every quad queue is observed, but only exact producers establish a
    // semantic layer in queues 0-3. Order alone is not ownership.
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110B7CU,
                    Oot3dPicaCompositionLayer::Unknown, Oot3dPicaCompositionProvenance::NativeControlFlow},
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110BACU,
                    Oot3dPicaCompositionLayer::Unknown, Oot3dPicaCompositionProvenance::NativeControlFlow},
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110C8CU,
                    Oot3dPicaCompositionLayer::Unknown, Oot3dPicaCompositionProvenance::NativeControlFlow},
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110CB0U,
                    Oot3dPicaCompositionLayer::Unknown, Oot3dPicaCompositionProvenance::NativeControlFlow},
    // 0x0011EF14: the scene's opaque, then translucent render list.
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EF3CU,
                    Oot3dPicaCompositionLayer::OpaqueWorld,
                    Oot3dPicaCompositionProvenance::NativeControlFlow},
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EF58U,
                    Oot3dPicaCompositionLayer::TransparentWorld,
                    Oot3dPicaCompositionProvenance::NativeControlFlow},
    // 0x0011EF60: the second visit of both lists, after translucency.
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EF88U,
                    Oot3dPicaCompositionLayer::TransparentWorld,
                    Oot3dPicaCompositionProvenance::NativeControlFlow},
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EFA4U,
                    Oot3dPicaCompositionLayer::TransparentWorld,
                    Oot3dPicaCompositionProvenance::NativeControlFlow},
    // 0x0011EFAC: the 2D scene's two lists.
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EFD4U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    ScopeDescriptor{kMm3dRenderListVisitEntry, 0x0011EFF4U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    // The sky, from the frame draw (0x001109F8) after it binds list 9.
    ScopeDescriptor{kMm3dSkyboxDrawEntry, 0x00110DF8U,
                    Oot3dPicaCompositionLayer::Atmosphere,
                    Oot3dPicaCompositionProvenance::NativeControlFlow},
    // A 2D layout queue (0x001267E8): each enqueued owner, from either of
    // its loops, and the queue's finisher.
    ScopeDescriptor{kMm3dLayoutOwnerDrawEntry, 0x00126998U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    ScopeDescriptor{kMm3dLayoutOwnerDrawEntry, 0x00126A64U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    ScopeDescriptor{kMm3dLayoutDrawEntry, 0x001269DCU,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    ScopeDescriptor{kMm3dLayoutDrawEntry, 0x00126AA8U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    // The frame draw's 2D quad queues 4 and 5, after the 2D scene pass
    // (0x00110D84 and 0x00110DB4 tail-call 0x0012C6A8 with the queue).
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110D88U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
    ScopeDescriptor{kMm3dQuadQueueDrawEntry, 0x00110DB8U,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
};

constexpr auto BuildHookPcs() {
    std::array<uint32_t, kMm3dLightingHookSites.size()> pcs{};
    for (size_t i = 0; i < pcs.size(); ++i) pcs[i] = kMm3dLightingHookSites[i].Pc;
    return pcs;
}
constexpr auto kHookPcs = BuildHookPcs();

// Spans older than this many scene frames are dropped: the GPU runs a list
// one guest frame after it is built.
constexpr uint64_t kSpanLifetimeSceneFrames = 2U;

bool WorldLayer(Oot3dPicaCompositionLayer layer) {
    return layer == Oot3dPicaCompositionLayer::OpaqueWorld ||
           layer == Oot3dPicaCompositionLayer::TransparentWorld;
}

} // namespace

std::span<const uint32_t> Mm3dNativePicaCompositionHookPcs() noexcept {
    return kHookPcs;
}

bool Mm3dNativePicaCompositionTracker::VerifyHooks(oot3d::recomp::a32::MemoryBus& memory,
    const oot3d::recomp::a32::Registry* registry) {
    mHooksVerified = false;
    for (const auto& hook : kMm3dLightingHookSites) {
        if (registry && !oot3d::recomp::a32::FindBlock(*registry, hook.Pc)) return false;
        for (uint32_t i = 0; i < hook.Words.size(); ++i) {
            uint32_t word = 0;
            if (!memory.Read32(hook.Pc + 4U * i, &word) || word != hook.Words[i]) return false;
        }
    }
    // Semantic actor owners: En_Mag (title logo) and Object_Kankyo. IDs
    // alone are insufficient; their draw callback and code must also agree.
    for (const auto [pc, word] : kMm3dCompositionOwnerGuards) {
        uint32_t actual = 0;
        if (!memory.Read32(pc, &actual) || actual != word) return false;
    }
    // Data guards only: a mismatch leaves the environment group Unknown.
    if (!mLighting.VerifyEnvironmentGroupGuards(memory))
        std::fprintf(stderr, "MM3D environment group: writer guard words differ; group stays unknown\n");
    mHooksVerified = true;
    return true;
}

Mm3dNativePicaCompositionTracker::Event
Mm3dNativePicaCompositionTracker::ObserveBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (!mHooksVerified) return Event::None;
    mLighting.ObserveBlockEntry(pc, state, memory);
    if (pc == 0x00191238U || pc == 0x001749C4U || pc == 0x00174EBCU || pc == 0x00191200U)
        InvalidateActor(pc == 0x00191238U ? state.r[0] : state.r[4]);
    if (pc == 0x005EC220U) {
        mModelLifetimes.erase(state.r[0]);
        for (auto& [slot, owner] : mQueueOwners)
            if (owner.Model == state.r[0])
                owner = {owner.Model, owner.Generation, 0, false, owner.List};
        ++mStats.LifecycleInvalidations;
    }
    if (pc == 0x0015E9ECU) {
        uint16_t scene = 0;
        uint32_t frame = 0;
        const uint32_t play = state.r[0];
        const bool valid = play != 0U && play <= UINT32_MAX - 0x14AU &&
            memory.Read16(play + 0x148U, &scene) && memory.Read32(play + 0x138U, &frame);
        if (!valid || play != mPlay || scene != mScene || frame < mPlayFrame) {
            mActorLifetimes.clear(); mModelLifetimes.clear(); mSpans.clear();
            ++mStats.LifecycleInvalidations;
        }
        mPlay = valid ? play : 0U; mScene = scene; mPlayFrame = frame;
        ++mQueueGeneration;
        mQueueOwners.clear(); mActorDrawStack.clear(); mModelSubmit.reset(); mModelScope.reset();
        mOverlaySubmit.reset();
        mPrimitiveScope.reset(); mPendingPrimitives.clear();
        mEnvironmentProducers.clear(); mQuadOwners.clear(); mQuadScope.reset();
        mActive.reset(); mFineSuppressed = false;
    }
    if (ObserveQuads(pc, state, memory)) return Event::None;
    if (ObservePrimitive(pc, state, memory)) return Event::None;
    if (pc == 0x00182C44U) {
        // Exact main callback BLX, before the separate native shadow callback.
        uint16_t id = 0;
        uint32_t callback = 0;
        uint64_t identity = 0;
        if (state.r[0] != 0U && state.r[0] <= UINT32_MAX - 0x148U &&
            memory.Read16(state.r[0], &id) && memory.Read32(state.r[0] + 0x144U, &callback) &&
            callback != 0U && callback == state.r[2]) identity = (uint64_t{id} << 32U) | state.r[0];
        if (identity) {
            auto& lifetime = mActorLifetimes[state.r[0]];
            if (lifetime.Identity != identity || !lifetime.Lifetime)
                lifetime = {identity, NextLifetime()};
        }
        mActorDrawStack.push_back(identity);
    } else if (pc == 0x00182C48U && !mActorDrawStack.empty()) mActorDrawStack.pop_back();
    if (pc == 0x0020A898U) {
        const uint32_t lr = state.r[14];
        mModelSubmit = ModelSubmit{state.r[0], lr, lr == 0x0021EFACU || lr == 0x001F3898U || lr == 0x001F3AB0U};
    } else if (pc == 0x0020A8B8U) {
        // The same scene-queue helper can also be entered directly.
        if (!mModelSubmit || state.r[1] != mModelSubmit->Model || state.r[14] != mModelSubmit->ReturnPc)
            mModelSubmit.reset();
    } else if (pc == 0x00223EDCU) mModelSubmit.reset();
    if (pc == 0x0011F000U) {
        // The 2D helper tail-calls the list append with its caller's LR.
        // Bind the exact model/list/return tuple, not an LR or model guess.
        uint32_t list = 0;
        mOverlaySubmit.reset();
        if (state.r[0] && state.r[0] <= UINT32_MAX - 8U &&
            memory.Read32(state.r[0] + 8U, &list) && list)
            mOverlaySubmit = OverlaySubmit{state.r[1], list, state.r[14]};
    }
    if (pc == 0x00224104U) {
        const bool overlayProducer = mOverlaySubmit && mOverlaySubmit->List == state.r[0] &&
            mOverlaySubmit->Model == state.r[1] && mOverlaySubmit->ReturnPc == state.r[14];
        mOverlaySubmit.reset();
        uint32_t count = 0;
        if (state.r[0] != 0U && state.r[0] <= UINT32_MAX - 0x408U &&
            memory.Read32(state.r[0] + 4U, &count) && count < 256U) {
            if (count == 0U) for (auto it = mQueueOwners.begin(); it != mQueueOwners.end();) {
                if (it->second.List == state.r[0]) it = mQueueOwners.erase(it); else ++it;
            }
            const uint32_t slot = state.r[0] + 8U + count * 4U;
            const bool producer = overlayProducer || state.r[14] == 0x0020A8DCU ||
                state.r[14] == 0x0020A8F8U || state.r[14] == 0x0020A918U;
            const bool shadow = producer && !overlayProducer && mModelSubmit && mModelSubmit->Model == state.r[1] && mModelSubmit->Shadow;
            // Always replace the slot, including unrelated producers; never key by model alone.
            QueueOwner owner{state.r[1], mQueueGeneration,
                !producer || shadow || mActorDrawStack.empty() ? 0U : mActorDrawStack.back(), shadow, state.r[0]};
            const auto actor = mActorLifetimes.find(uint32_t(owner.Actor));
            uint32_t vtable = 0, callback = 0, primary = 0, secondary = 0;
            const bool liveActor = owner.Actor && actor != mActorLifetimes.end() && actor->second.Identity == owner.Actor;
            // Room/background models use the proved Play/scene owner rather
            // than masquerading as actors. Their model destructor and scene
            // reset provide the same lifetime boundaries as actor models.
            const uint64_t temporalOwner = liveActor ? owner.Actor :
                mActorDrawStack.empty() && mPlay ? (1ULL << 63U) | (uint64_t{mScene} << 32U) | mPlay : 0U;
            const uint64_t ownerEpoch = liveActor ? actor->second.Lifetime : 0U;
            // Semantic ownership belongs to the guarded actor callback, not
            // the CMB temporal ABI: effects and 2D models may have other vtables.
            if (liveActor && memory.Read32(uint32_t(owner.Actor) + 0x144U, &callback)) {
                owner.Isolated = (owner.Actor >> 32U) == 0x87U && callback == 0x0039F068U;
                owner.Atmosphere = (owner.Actor >> 32U) == 0x41U && callback == 0x004C03F4U;
            }
            if (producer && !shadow && !mEnvironmentProducers.empty()) owner.Atmosphere = true;
            if (producer && !shadow && temporalOwner &&
                state.r[1] <= UINT32_MAX - 0xA4U &&
                memory.Read32(state.r[1], &vtable) && vtable == 0x0063BED4U &&
                memory.Read32(state.r[1] + 0xA0U, &primary) && memory.Read32(state.r[1] + 0xA4U, &secondary)) {
                auto& model = mModelLifetimes[state.r[1]];
                if (model.Owner != temporalOwner || model.OwnerEpoch != ownerEpoch || !model.Lifetime ||
                    model.Primary != primary || model.Secondary != secondary)
                    model = {temporalOwner, ownerEpoch, NextLifetime(), primary, secondary};
                owner.Lifetime = model.Lifetime;
            }
            mQueueOwners[slot] = owner;
        }
        return Event::None;
    }
    if (pc == kMm3dRenderListVisitEntry) {
        if (mActive) {
            // No proved nested-pass contract: drop the unclosed outer range.
            ++mStats.NestedScopeEntries; mModelScope.reset(); mActive.reset();
            mPrimitiveScope.reset(); mPendingPrimitives.clear();
            mFineSuppressed = true;
            return Event::None;
        }
        // MM reorders some lists before visiting them. Reconcile a complete
        // same-list/same-generation permutation, never a global model lookup.
        const uint32_t list = state.r[0];
        uint32_t count = 0;
        std::vector<QueueOwner> before;
        for (const auto& [slot, owner] : mQueueOwners)
            if (owner.List == list && owner.Generation == mQueueGeneration) before.push_back(owner);
        bool complete = list != 0U && list <= UINT32_MAX - 0x408U &&
            memory.Read32(list + 4U, &count) && count <= 256U && before.size() == count;
        std::vector<QueueOwner> after;
        for (uint32_t i = 0; complete && i < count; ++i) {
            uint32_t model = 0;
            if (!memory.Read32(list + 8U + i * 4U, &model)) { complete = false; break; }
            const auto found = std::find_if(before.begin(), before.end(), [model](const auto& owner) { return owner.Model == model; });
            if (found == before.end()) { complete = false; break; }
            QueueOwner owner = *found;
            // Identical model handles with different owners cannot establish a
            // permutation by observation. Do not transfer either actor/shadow role.
            if (std::any_of(before.begin(), before.end(), [&](const auto& other) {
                return other.Model == model && (other.Actor != owner.Actor || other.Shadow != owner.Shadow ||
                    other.Lifetime != owner.Lifetime || other.Isolated != owner.Isolated || other.Atmosphere != owner.Atmosphere);
            })) {
                for (auto& other : before) if (other.Model == model) {
                    other.Actor = 0; other.Shadow = false; other.Lifetime = 0;
                    other.Isolated = false; other.Atmosphere = false;
                }
                owner.Actor = 0; owner.Shadow = false; owner.Lifetime = 0;
                owner.Isolated = false; owner.Atmosphere = false;
            }
            after.push_back(owner); before.erase(found);
        }
        for (auto it = mQueueOwners.begin(); it != mQueueOwners.end();) {
            if (it->second.List == list) it = mQueueOwners.erase(it); else ++it;
        }
        if (complete && before.empty()) for (uint32_t i = 0; i < after.size(); ++i)
            mQueueOwners[list + 8U + i * 4U] = after[i];
    }
    if (mModelScope && pc == mModelScope->ReturnPc) {
        const auto model = *mModelScope;
        mModelScope.reset();
        const uint64_t slot = pc == 0x005EF0D0U ? uint64_t{state.r[5]} : uint64_t{state.r[4]} + 8U + 4ULL * state.r[5];
        uint32_t end = 0;
        if (mActive && slot == model.Slot && memory.Read32(kMm3dCommandListCursorAddress, &end)) {
            if (end >= model.Begin && end - model.Begin <= 0x00400000U && !(end & 7U) && !(model.Begin & 7U)) {
                if (end != model.Begin) Record(model.Begin, end, model.Attribution, model.Lighting);
                else ++mStats.EmptyScopes;
                FlushPrimitives();
                mActive->BeginAddress = end;
            } else {
                ++mStats.InvalidRanges; mActive.reset(); mFineSuppressed = true;
                mPrimitiveScope.reset(); mPendingPrimitives.clear();
            }
        } else {
            ++mStats.InvalidRanges;
            // Never back-fill a failed child with possibly stale actor/light ownership.
            mActive.reset(); mPrimitiveScope.reset(); mPendingPrimitives.clear();
        }
        return Event::None;
    }
    if ((pc == 0x005EF0CCU || pc == 0x005EF094U) && mActive &&
        (WorldLayer(mActive->Attribution.Layer) || mActive->Attribution.Layer == Oot3dPicaCompositionLayer::Ui)) {
        if (mFineSuppressed) return Event::None;
        if (mModelScope) {
            ++mStats.NestedScopeEntries;
            mModelScope.reset(); mFineSuppressed = true;
            mPrimitiveScope.reset(); mPendingPrimitives.clear();
            // Parent coarse metadata covers the unproved nested portion.
            return Event::None;
        }
        const uint64_t slot = pc == 0x005EF0CCU ? uint64_t{state.r[5]} : uint64_t{state.r[4]} + 8U + 4ULL * state.r[5];
        uint32_t model = 0, begin = 0;
        if (slot > UINT32_MAX - 4U || !memory.Read32(uint32_t(slot), &model) || model != state.r[0] ||
            !memory.Read32(kMm3dCommandListCursorAddress, &begin) || (begin & 7U)) return Event::None;
        if (begin < mActive->BeginAddress || begin - mActive->BeginAddress > 0x00400000U ||
            (mActive->BeginAddress & 7U)) {
            ++mStats.InvalidRanges; mActive.reset(); mFineSuppressed = true;
            return Event::None;
        }
        if (begin > mActive->BeginAddress)
            Record(mActive->BeginAddress, begin, mActive->Attribution, mActive->LightBinding);
        FlushPrimitives();
        ModelScope child;
        child.Begin = begin; child.Slot = uint32_t(slot);
        child.ReturnPc = pc + 4U; child.Attribution = mActive->Attribution;
        child.Attribution.SourcePc = pc;
        child.Lighting = mLighting.CaptureRenderer(0U, memory);
        const auto owner = mQueueOwners.find(uint32_t(slot));
        if (owner != mQueueOwners.end() && owner->second.Generation == mQueueGeneration && owner->second.Model == model) {
            child.Attribution.NativeActorIdentity = owner->second.Actor;
            child.Attribution.NativeActorGeometry = owner->second.Actor != 0;
            child.Attribution.NativeProjectedShadow = owner->second.Shadow;
            child.Attribution.IsolatedLighting = owner->second.Isolated;
            child.Attribution.ExcludeFromPresentationFxaa |= owner->second.Isolated;
            if (owner->second.Isolated) ++mStats.IsolatedModelSpans;
            if (owner->second.Atmosphere && WorldLayer(child.Attribution.Layer)) {
                child.Attribution.Layer = Oot3dPicaCompositionLayer::Atmosphere;
                ++mStats.AtmosphereModelSpans;
            }
            const auto lifetime = mModelLifetimes.find(model);
            if (WorldLayer(child.Attribution.Layer) && !child.Attribution.IsolatedLighting && owner->second.Lifetime &&
                lifetime != mModelLifetimes.end() && lifetime->second.Lifetime == owner->second.Lifetime) {
                child.Attribution.TemporalIdentity = {lifetime->second.Owner, model, 1U,
                    uint64_t{mActive->ReturnPc} << 32U, owner->second.Lifetime};
                child.Attribution.TemporalCommandBufferIdentity = true;
                ++mStats.TemporalModelSpans;
            }
        }
        // Proven model factory/vtable ABI. +90 is a resource/preparation object.
        uint32_t vtable = 0, primary = 0, secondary = 0;
        if (model && model <= UINT32_MAX - 0xACU && memory.Read32(model, &vtable) && vtable == 0x0063BED4U &&
            memory.Read32(model + 0xA0U, &primary) && memory.Read32(model + 0xA4U, &secondary)) {
            uint32_t a = 0, b = 0;
            // If two renderers differ, a whole-model span cannot claim either
            // one's selected native slots. Scene payloads and actor role survive.
            if (primary && primary <= UINT32_MAX - 0x14U && memory.Read32(primary + 0x10U, &a) &&
                (secondary == 0U || (secondary <= UINT32_MAX - 0x14U && memory.Read32(secondary + 0x10U, &b) && a == b)))
                child.Lighting = mLighting.CaptureRenderer(primary, memory);
        }
        mActive->BeginAddress = begin;
        mModelScope = std::move(child);
        return Event::None;
    }

    if (mActive.has_value() && pc == mActive->ReturnPc) {
        mQuadScope.reset();
        mModelScope.reset(); // An unclosed child cannot survive its parent.
        const ActiveScope scope = *mActive;
        mActive.reset();
        ++mStats.ScopeExits;
        uint32_t end = 0;
        if (!memory.Read32(kMm3dCommandListCursorAddress, &end)) {
            ++mStats.ReadFailures;
            mPrimitiveScope.reset(); mPendingPrimitives.clear();
            return Event::None;
        }
        if (end == scope.BeginAddress) {
            ++mStats.EmptyScopes;
        } else if (end < scope.BeginAddress || (end & 7U) != 0U ||
                   (scope.BeginAddress & 7U) != 0U ||
                   end - scope.BeginAddress > 0x00400000U) {
            // The pass ended in another list than it began in.
            ++mStats.InvalidRanges;
        } else {
            Record(scope.BeginAddress, end, scope.Attribution, scope.LightBinding);
        }
        if (end >= scope.BeginAddress && end - scope.BeginAddress <= 0x00400000U &&
            !(end & 7U) && !(scope.BeginAddress & 7U)) FlushPrimitives();
        else mPendingPrimitives.clear();
        mPrimitiveScope.reset();
        return Event::None;
    }
    const auto scope = std::find_if(
        kScopes.begin(), kScopes.end(), [&](const ScopeDescriptor& candidate) {
            return candidate.EntryPc == pc && candidate.ReturnPc == state.r[14];
        });
    if (scope == kScopes.end()) {
        return Event::None;
    }
    if (pc == kMm3dQuadQueueDrawEntry) {
        constexpr std::array returns{0x00110B7CU, 0x00110BACU, 0x00110C8CU,
                                    0x00110CB0U, 0x00110D88U, 0x00110DB8U};
        if (state.r[1] >= returns.size() || returns[state.r[1]] != state.r[14]) {
            ++mStats.InvalidQuadOwners; return Event::None;
        }
    }
    if (mActive.has_value()) {
        ++mStats.NestedScopeEntries;
        return Event::None;
    }
    uint32_t begin = 0;
    if (!memory.Read32(kMm3dCommandListCursorAddress, &begin)) {
        ++mStats.ReadFailures;
        return Event::None;
    }
    ++mStats.ScopeEntries;
    if (scope->Layer == Oot3dPicaCompositionLayer::OpaqueWorld) {
        CaptureSceneLighting(memory);
    }
    mFineSuppressed = false;
    mActive = ActiveScope{scope->ReturnPc, begin,
                          {scope->Layer, scope->Provenance, scope->EntryPc,
                           scope->ReturnPc},
                          WorldLayer(scope->Layer)
                              ? mSceneLighting
                              : Fast::Renderer3ds::PicaNativeLightBinding{}};
    mActive->Attribution.ExcludeFromPresentationFxaa = scope->Layer == Oot3dPicaCompositionLayer::Ui;
    if (pc == kMm3dQuadQueueDrawEntry) mQuadQueue = state.r[1];
    if (scope->Layer == Oot3dPicaCompositionLayer::OpaqueWorld) {
        ++mStats.SceneFrames;
        const uint64_t frame = mStats.SceneFrames;
        const auto expired = std::remove_if(
            mSpans.begin(), mSpans.end(), [frame](const RecordedSpan& span) {
                return span.SceneFrame + kSpanLifetimeSceneFrames < frame;
            });
        mStats.ExpiredSpans += static_cast<uint64_t>(mSpans.end() - expired);
        mSpans.erase(expired, mSpans.end());
        return Event::OpaqueWorldBegins;
    }
    return Event::None;
}

bool Mm3dNativePicaCompositionTracker::ObserveQuads(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    constexpr std::array environments{
        EnvironmentProducer{0x00172D18U, 0x0015ED08U},
        EnvironmentProducer{0x0016FD3CU, 0x0015F0B4U},
        EnvironmentProducer{0x00171BE0U, 0x0015EF7CU},
        EnvironmentProducer{0x00171B40U, 0x0015EF58U},
        EnvironmentProducer{0x00171B40U, 0x0015F070U},
        EnvironmentProducer{0x00171B40U, 0x0015F098U},
        EnvironmentProducer{0x00171B40U, 0x0015F0DCU}};
    if (!mEnvironmentProducers.empty() && pc == mEnvironmentProducers.back().Return)
        mEnvironmentProducers.pop_back();
    for (const auto& producer : environments)
        if (pc == producer.Entry && state.r[14] == producer.Return) {
            if (mEnvironmentProducers.size() < 8) mEnvironmentProducers.push_back(producer);
            else { mEnvironmentProducers.clear(); ++mStats.InvalidQuadOwners; }
            return true;
        }
    if (pc == 0x00179930U || pc == 0x00179AD8U) {
        const uint32_t manager = state.r[pc == 0x00179930U ? 0 : 6];
        const uint32_t queue = state.r[pc == 0x00179930U ? 1 : 9];
        const uint32_t model = state.r[pc == 0x00179930U ? 2 : 4];
        uint32_t count = 0;
        if (!model || !manager || queue > 6 || uint64_t{manager} + 0x3000U > UINT32_MAX ||
            !memory.Read32(manager + 0x14U + queue * 4U, &count) || count >= 0x180U) return true;
        QuadOwner owner;
        owner.Model = model; owner.Generation = mQueueGeneration;
        owner.Attribution = {queue >= 4 ? Oot3dPicaCompositionLayer::Ui : Oot3dPicaCompositionLayer::Unknown,
            Oot3dPicaCompositionProvenance::NativeControlFlow, pc, queue};
        if (!mEnvironmentProducers.empty()) {
            owner.Attribution.Layer = Oot3dPicaCompositionLayer::Atmosphere;
            owner.Attribution.SourcePc = mEnvironmentProducers.back().Entry;
        } else if (!mActorDrawStack.empty()) {
            const uint64_t actor = mActorDrawStack.back();
            uint32_t callback = 0;
            if ((actor >> 32U) == 0x41U && uint32_t(actor) <= UINT32_MAX - 0x144U &&
                memory.Read32(uint32_t(actor) + 0x144U, &callback) && callback == 0x004C03F4U)
                owner.Attribution.Layer = Oot3dPicaCompositionLayer::Atmosphere;
        }
        owner.Attribution.ExcludeFromPresentationFxaa = owner.Attribution.Layer == Oot3dPicaCompositionLayer::Ui;
        mQuadOwners[manager + 0x578U + queue * 0x600U + count * 4U] = owner;
        return true;
    }
    if (pc == 0x0012C730U && mQuadScope) {
        const auto child = *mQuadScope;
        mQuadScope.reset();
        const uint64_t slot = uint64_t{state.r[6]} + 0x578U + uint64_t{state.r[4]} * 4U;
        uint32_t end = 0;
        if (!mActive || slot != child.Slot || !memory.Read32(kMm3dCommandListCursorAddress, &end) ||
            end < child.Begin || end - child.Begin > 0x400000U || (end & 7U) || (child.Begin & 7U)) {
            ++mStats.InvalidQuadOwners; mPendingPrimitives.clear(); mActive.reset(); return true;
        }
        Record(child.Begin, end, child.Attribution, child.Lighting);
        FlushPrimitives(); mActive->BeginAddress = end;
        if (end > child.Begin) {
            if (child.Attribution.Layer == Oot3dPicaCompositionLayer::Atmosphere) ++mStats.AtmosphereQuadSpans;
            else if (child.Attribution.Layer == Oot3dPicaCompositionLayer::Ui) ++mStats.UiQuadSpans;
            else if (mQuadQueue < mStats.UnknownQuadSpans.size()) ++mStats.UnknownQuadSpans[mQuadQueue];
        }
        return true;
    }
    if (pc != 0x0012C71CU || !mActive || mActive->Attribution.SourcePc != kMm3dQuadQueueDrawEntry)
        return false;
    if (mQuadScope) { ++mStats.InvalidQuadOwners; mQuadScope.reset(); mActive.reset(); return true; }
    const uint64_t slot = uint64_t{state.r[6]} + 0x578U + uint64_t{state.r[4]} * 4U;
    uint32_t model = 0, begin = 0;
    if (state.r[4] >= 0x180U || slot > UINT32_MAX - 4U ||
        !memory.Read32(uint32_t(slot), &model) || !memory.Read32(kMm3dCommandListCursorAddress, &begin) ||
        begin < mActive->BeginAddress || begin - mActive->BeginAddress > 0x400000U ||
        (begin & 7U) || (mActive->BeginAddress & 7U)) {
        ++mStats.InvalidQuadOwners; return true;
    }
    Record(mActive->BeginAddress, begin, mActive->Attribution, mActive->LightBinding);
    FlushPrimitives();
    ModelScope child;
    child.Slot = uint32_t(slot); child.Begin = begin; child.Attribution = mActive->Attribution;
    child.Attribution.Layer = Oot3dPicaCompositionLayer::Unknown;
    child.Attribution.ExcludeFromPresentationFxaa = false;
    const auto owner = mQuadOwners.find(uint32_t(slot));
    if (owner != mQuadOwners.end() && owner->second.Model == model && owner->second.Generation == mQueueGeneration)
        child.Attribution = owner->second.Attribution;
    // Atmosphere is excluded from scene relighting, but guide-space transforms
    // still belong to this view. No actor/temporal identity is invented for quads.
    if (child.Attribution.Layer == Oot3dPicaCompositionLayer::Atmosphere)
        child.Lighting.WorldToView = mSceneLighting.WorldToView;
    mQuadScope = std::move(child);
    return true;
}

bool Mm3dNativePicaCompositionTracker::ObservePrimitive(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (pc == 0x001565BCU) {
        if (!mPrimitiveScope) return true;
        const auto primitive = *mPrimitiveScope;
        mPrimitiveScope.reset();
        uint32_t end = 0;
        if (!memory.Read32(primitive.Context + 8U, &end) || end <= primitive.Begin ||
            end - primitive.Begin >= 0x10000U || (end & 7U) || (primitive.Begin & 7U)) {
            ++mStats.InvalidPrimitives;
            return true;
        }
        mPendingPrimitives.push_back({primitive.Begin, end, primitive.Attribution, primitive.Lighting});
        ++mStats.PrimitiveSpans;
        if (primitive.Attribution.TemporalIdentity.Available()) ++mStats.TemporalPrimitiveSpans;
        return true;
    }
    if (pc != 0x001565ACU) return false;
    ++mStats.PrimitiveEntries;
    if (mPrimitiveScope) {
        mPrimitiveScope.reset(); ++mStats.InvalidPrimitives;
        return true;
    }
    constexpr std::array returns{0x001323F8U, 0x001413C0U, 0x005E320CU, 0x005E32F8U,
                                0x005E4218U, 0x005E9124U, 0x005E9210U, 0x005EA1CCU};
    if (std::find(returns.begin(), returns.end(), state.r[14]) == returns.end() ||
        mFineSuppressed || !mActive) {
        ++mStats.UnownedPrimitives; return true;
    }
    PrimitiveScope primitive;
    primitive.OuterReturn = state.r[14];
    if (!state.r[0] || !memory.Read32(state.r[0], &primitive.Context) ||
        !primitive.Context || primitive.Context > UINT32_MAX - 8U ||
        !memory.Read32(primitive.Context + 8U, &primitive.Begin)) {
        ++mStats.InvalidPrimitives; return true;
    }
    primitive.Attribution = mQuadScope ? mQuadScope->Attribution :
        mModelScope ? mModelScope->Attribution : mActive->Attribution;
    primitive.Lighting = mQuadScope ? mQuadScope->Lighting :
        mModelScope ? mModelScope->Lighting : mActive->LightBinding;
    const auto ownerIdentity = primitive.Attribution.TemporalIdentity;
    primitive.Attribution.TemporalIdentity = {};
    primitive.Attribution.TemporalCommandBufferIdentity = false;
    primitive.Attribution.TemporalPacketBeginAddress = 0;
    primitive.Attribution.SourcePc = pc;
    primitive.Attribution.NativeValue = state.r[14];
    // Native group/record ABI in the six model-renderer callers. An effect
    // builder outside a proved model/renderer association gets composition,
    // never an identity fabricated from the builder or destination address.
    const bool mixed = state.r[14] == 0x005E320CU || state.r[14] == 0x005E9124U;
    const uint32_t group = state.r[mixed ? 8 : 7], index = state.r[mixed ? 6 : 5];
    uint32_t model = 0, primary = 0, secondary = 0, builder = 0, count = 0;
    uint16_t vertices = 0;
    if (mModelScope && ownerIdentity.Available() && WorldLayer(primitive.Attribution.Layer) &&
        state.r[14] >= 0x005E0000U && group && index < 0xFFFFU &&
        uint64_t{group} + 0x16U + uint64_t{index} * 8U <= UINT32_MAX &&
        memory.Read32(group + 8U, &count) && count <= 0xFFFFU && index < count &&
        memory.Read16(group + 0x14U + index * 8U, &vertices) && vertices == state.r[2] &&
        memory.Read32(mModelScope->Slot, &model) && model <= UINT32_MAX - 0xA4U &&
        memory.Read32(model + 0xA0U, &primary) && memory.Read32(model + 0xA4U, &secondary) &&
        state.r[4] && (state.r[4] == primary || state.r[4] == secondary) &&
        state.r[4] <= UINT32_MAX - 0x14U && memory.Read32(state.r[4] + 0x14U, &builder) &&
        builder == state.r[0]) {
        primitive.Attribution.TemporalIdentity = ownerIdentity;
        primitive.Attribution.TemporalIdentity.Part = group;
        primitive.Attribution.TemporalIdentity.Slot = (ownerIdentity.Slot & 0xFFFFFFFF00000000ULL) |
            ((uint64_t{index} + 1U) << 16U);
        primitive.Attribution.TemporalPacketBeginAddress = primitive.Begin;
        primitive.Attribution.TemporalPacketSourceRelative = true;
    }
    mPrimitiveScope = std::move(primitive);
    return true;
}

void Mm3dNativePicaCompositionTracker::FlushPrimitives() {
    // A coarse closing scope must not overwrite its already-observed children.
    for (const auto& span : mPendingPrimitives)
        Record(span.BeginAddress, span.EndAddress, span.Attribution, span.LightBinding);
    mPendingPrimitives.clear();
}

void Mm3dNativePicaCompositionTracker::CaptureSceneLighting(
    oot3d::recomp::a32::MemoryBus& memory) {
    const auto current = CaptureMm3dSceneEnvironment(memory);
    mLighting.CaptureSceneLights(current.Play, memory);
    mSceneLighting = mLighting.CaptureRenderer(0U, memory);
    mStats.SceneDirectionalLights = mSceneLighting.SceneDirectionals ? mSceneLighting.SceneDirectionals->Count : 0U;
    if (mSceneLighting.WorldToView) ++mStats.LightBindings;
}

void Mm3dNativePicaCompositionTracker::Record(
    uint32_t begin, uint32_t end,
    const Oot3dPicaCompositionAttribution& attribution,
    const Fast::Renderer3ds::PicaNativeLightBinding& lightBinding) {
    // Clip old coverage around a precise child range. A model must neither
    // erase the rest of its pass nor be erased by a closing parent scope.
    if (begin >= end) return;
    std::vector<RecordedSpan> kept;
    for (const auto& old : mSpans) {
        if (old.Span.EndAddress <= begin || old.Span.BeginAddress >= end) { kept.push_back(old); continue; }
        ++mStats.ReplacedSpans;
        if (old.Span.BeginAddress < begin) { auto left = old; left.Span.EndAddress = begin; kept.push_back(left); }
        if (old.Span.EndAddress > end) { auto right = old; right.Span.BeginAddress = end; kept.push_back(right); }
    }
    kept.push_back({{begin, end, attribution, lightBinding}, mStats.SceneFrames});
    std::sort(kept.begin(), kept.end(), [](const auto& a, const auto& b) { return a.Span.BeginAddress < b.Span.BeginAddress; });
    mSpans = std::move(kept);
    ++mStats.RecordedSpans;
    return;

}

bool Mm3dNativePicaCompositionTracker::TakeCommandListCompositionSpans(
    uint32_t, uint32_t,
    std::vector<Oot3dPicaCommandListCompositionSpan>& spans, std::string*) {
    spans.clear();
    spans.reserve(mSpans.size());
    for (const auto& recorded : mSpans) {
        spans.push_back(recorded.Span);
    }
    return true;
}

uint64_t Mm3dNativePicaCompositionTracker::NextLifetime() noexcept {
    if (++mNextLifetime == 0) ++mNextLifetime;
    return mNextLifetime;
}

void Mm3dNativePicaCompositionTracker::InvalidateActor(uint32_t actor) {
    mActorLifetimes.erase(actor);
    for (auto& [slot, owner] : mQueueOwners)
        if (uint32_t(owner.Actor) == actor)
            owner = {owner.Model, owner.Generation, 0, false, owner.List};
    for (auto it = mModelLifetimes.begin(); it != mModelLifetimes.end();) {
        if (!(it->second.Owner & (1ULL << 63U)) && uint32_t(it->second.Owner) == actor)
            it = mModelLifetimes.erase(it);
        else ++it;
    }
    ++mStats.LifecycleInvalidations;
}

void Mm3dNativePicaCompositionTracker::Reset() noexcept {
    // Never rewind the monotonic allocator on state load: the same guest
    // addresses after restore must not match an earlier presented sample.
    mActorLifetimes.clear(); mModelLifetimes.clear();
    mPlay = mPlayFrame = 0; mScene = 0;
    mActive.reset();
    mSpans.clear();
    mSceneLighting = {};
    mLighting.Reset();
    mActorDrawStack.clear(); mQueueOwners.clear(); mModelSubmit.reset(); mModelScope.reset();
    mOverlaySubmit.reset();
    mPrimitiveScope.reset(); mPendingPrimitives.clear();
    mEnvironmentProducers.clear(); mQuadOwners.clear(); mQuadScope.reset();
    mFineSuppressed = false;
    ++mQueueGeneration;
    mStats = {};
}

const Mm3dNativePicaCompositionStats&
Mm3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
