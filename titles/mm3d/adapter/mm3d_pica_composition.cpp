#include "mm3d_pica_composition.h"

#include "mm3d_title_contracts.h"
#include "mm3d_ui_subsystems.h"
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
    // Data guards only: a mismatch leaves the environment group Unknown.
    if (!mLighting.VerifyEnvironmentGroupGuards(memory))
        std::fprintf(stderr, "MM3D environment group: writer guard words differ; group stays unknown\n");
    mHooksVerified = true;
    return true;
}

// P17b diagnostic: names the sprites a quad queue (manager 0x0071B000,
// counts +0x14, objects +0x578, mm3d-dekui native_minimap.h) draws.
void Mm3dNativePicaCompositionTracker::RecordQuadObjects(
    oot3d::recomp::a32::MemoryBus& memory, uint32_t queue) {
    constexpr uint32_t kManager = 0x0071B000U, kCapacity = 0x180U;
    uint32_t count = 0;
    if (queue > 7U || !memory.Read32(kManager + 0x14U + queue * 4U, &count) || count > kCapacity) return;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t object = 0, vtable = 0, quads = 0, positions = 0, x = 0, y = 0;
        if (!memory.Read32(kManager + 0x578U + queue * 0x600U + i * 4U, &object) ||
            object < 0x100000U || (object & 3U) || object > 0x3FFFFC00U ||
            !memory.Read32(object, &vtable) || !memory.Read32(object + 0x2C8U, &quads)) continue;
        float fx = 0.0F, fy = 0.0F;
        if (quads && memory.Read32(object + 0x2B8U, &positions) && positions >= 0x100000U &&
            !(positions & 3U) && positions < 0x3FFFFFF0U && memory.Read32(positions, &x) &&
            memory.Read32(positions + 4U, &y)) {
            fx = std::bit_cast<float>(x); fy = std::bit_cast<float>(y);
        }
        char key[96];
        std::snprintf(key, sizeof key, "q%u vt=%08X n=%u at=%.0f,%.0f", queue, vtable, quads,
                      std::isfinite(fx) ? fx : -1.0F, std::isfinite(fy) ? fy : -1.0F);
        auto found = mStats.QuadObjects.find(key);
        if (found != mStats.QuadObjects.end()) ++found->second;
        else if (mStats.QuadObjects.size() < 64U) mStats.QuadObjects.emplace(key, 1U);
        else ++mStats.QuadObjectsDropped;
    }
}

Mm3dNativePicaCompositionTracker::Event
Mm3dNativePicaCompositionTracker::ObserveBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (!mHooksVerified) return Event::None;
    mLighting.ObserveBlockEntry(pc, state, memory);
    if (pc == 0x0015E9ECU) {
        ++mQueueGeneration;
        mQueueOwners.clear(); mActorDrawStack.clear(); mModelSubmit.reset(); mModelScope.reset();
        mActive.reset(); mFineSuppressed = false;
    }
    if (pc == 0x00182C44U) {
        // Exact main callback BLX, before the separate native shadow callback.
        uint16_t id = 0;
        uint32_t callback = 0;
        uint64_t identity = 0;
        if (state.r[0] != 0U && state.r[0] <= UINT32_MAX - 0x148U &&
            memory.Read16(state.r[0], &id) && memory.Read32(state.r[0] + 0x144U, &callback) &&
            callback != 0U && callback == state.r[2]) identity = (uint64_t{id} << 32U) | state.r[0];
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
    if (pc == 0x00224104U) {
        uint32_t count = 0;
        if (state.r[0] != 0U && state.r[0] <= UINT32_MAX - 0x408U &&
            memory.Read32(state.r[0] + 4U, &count) && count < 256U) {
            if (count == 0U) for (auto it = mQueueOwners.begin(); it != mQueueOwners.end();) {
                if (it->second.List == state.r[0]) it = mQueueOwners.erase(it); else ++it;
            }
            const uint32_t slot = state.r[0] + 8U + count * 4U;
            const bool producer = state.r[14] == 0x0020A8DCU || state.r[14] == 0x0020A8F8U || state.r[14] == 0x0020A918U;
            const bool shadow = producer && mModelSubmit && mModelSubmit->Model == state.r[1] && mModelSubmit->Shadow;
            // Always replace the slot, including unrelated producers; never key by model alone.
            mQueueOwners[slot] = {state.r[1], mQueueGeneration,
                shadow || mActorDrawStack.empty() ? 0U : mActorDrawStack.back(), shadow, state.r[0]};
        }
        return Event::None;
    }
    if (pc == kMm3dRenderListVisitEntry) {
        if (mActive) {
            // No proved nested-pass contract: drop the unclosed outer range.
            ++mStats.NestedScopeEntries; mModelScope.reset(); mActive.reset();
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
                return other.Model == model && (other.Actor != owner.Actor || other.Shadow != owner.Shadow);
            })) {
                for (auto& other : before) if (other.Model == model) { other.Actor = 0; other.Shadow = false; }
                owner.Actor = 0; owner.Shadow = false;
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
                mActive->BeginAddress = end;
            } else {
                ++mStats.InvalidRanges; mActive.reset(); mFineSuppressed = true;
            }
        } else {
            ++mStats.InvalidRanges;
            // Never back-fill a failed child with possibly stale actor/light ownership.
            mActive.reset();
        }
        return Event::None;
    }
    if ((pc == 0x005EF0CCU || pc == 0x005EF094U) && mActive && WorldLayer(mActive->Attribution.Layer)) {
        if (mFineSuppressed) return Event::None;
        if (mModelScope) {
            ++mStats.NestedScopeEntries;
            mModelScope.reset(); mFineSuppressed = true;
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
        mModelScope.reset(); // An unclosed child cannot survive its parent.
        const ActiveScope scope = *mActive;
        mActive.reset();
        ++mStats.ScopeExits;
        uint32_t end = 0;
        if (!memory.Read32(kMm3dCommandListCursorAddress, &end)) {
            ++mStats.ReadFailures;
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
        return Event::None;
    }
    const auto scope = std::find_if(
        kScopes.begin(), kScopes.end(), [&](const ScopeDescriptor& candidate) {
            return candidate.EntryPc == pc && candidate.ReturnPc == state.r[14];
        });
    if (scope == kScopes.end()) {
        return Event::None;
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
    if (scope->EntryPc == kMm3dLayoutOwnerDrawEntry) {
        std::string name;
        const auto subsystem = ReadMm3dLayoutName(memory, state.r[1], &name)
            ? ClassifyMm3dLayout(name) : Mm3dUiSubsystem::Unknown;
        if (!name.empty()) {
            auto owner = mStats.LayoutOwners.find(name);
            if (owner != mStats.LayoutOwners.end()) ++owner->second;
            else if (mStats.LayoutOwners.size() < 64U) mStats.LayoutOwners.emplace(std::move(name), 1U);
            else ++mStats.LayoutOwnersDropped;
        }
        if (subsystem != Mm3dUiSubsystem::Unknown)
            mActive->Attribution.NativeValue = kMm3dUiTag | uint32_t(subsystem);
    }
    if (scope->EntryPc == kMm3dQuadQueueDrawEntry) RecordQuadObjects(memory, state.r[1]);
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

void Mm3dNativePicaCompositionTracker::Reset() noexcept {
    mActive.reset();
    mSpans.clear();
    mSceneLighting = {};
    mLighting.Reset();
    mActorDrawStack.clear(); mQueueOwners.clear(); mModelSubmit.reset(); mModelScope.reset();
    mFineSuppressed = false;
    ++mQueueGeneration;
    mStats = {};
}

const Mm3dNativePicaCompositionStats&
Mm3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
