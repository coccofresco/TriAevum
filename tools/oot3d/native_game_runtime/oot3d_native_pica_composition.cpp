#include "oot3d_native_pica_composition.h"

#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kPicaCommandListCursorAddress = 0x0054CC4CU;
constexpr uint32_t kMeshPacketByteCountOffset = 0x10U;
// Proven ABI: oot3d_typed_actor_lifecycle.cpp reads actor r0 at destroy
// entry and actor r4 at the UpdateAll init-callback dispatch.
constexpr uint32_t kTemporalActorDestroy = 0x002D644CU;
constexpr uint32_t kTemporalActorInit = 0x004615D8U;
// Exact UI draw owners from ui_native_workflow_closure. The enclosing
// DrawViewPass also executes scene callbacks and must NOT be classified UI.
constexpr std::array kUiViews{
    std::pair{0x0042B9F4U, 0x003004E0U},
    std::pair{0x0041EC50U, 0x00419830U},
    // TopScreen System overlay inside the completed gameplay view.
    std::pair{0x0041EC50U, 0x0030055CU},
    std::pair{0x0041F308U, 0x0041983CU},
    std::pair{0x00425930U, 0x0041EB14U},
    // Orthographic renderer-local backdrop (single model at GraphRenderer+743C).
    std::pair{0x002FFF7CU, 0x00419824U},
    // Renderer orthographic deferred-model queue 6 (including Hint/Visions).
    // 0041AFAC installs its own UI projection before consuming the queue;
    // neither the shared queue executor nor its update/enqueue callers own UI.
    std::pair{0x0041AFACU, 0x00419890U},
    // Screen-space overlay queue 4, consumed after DrawViewPass's UI projection.
    // Includes Hint primitives enqueued earlier by 0047087C/00483D10.
    std::pair{0x004228E4U, 0x00300524U},
    // HintMovie_Init installs these two native UI dispatch callbacks in
    // GraphRenderer+7440/+7444. Only their exact invocation sites are UI.
    std::pair{0x00471F60U, 0x0030051CU},
    std::pair{0x00477E30U, 0x0041984CU},
    // Message renderers in DrawViewPass: backdrop/choice, auxiliary glyphs,
    // contextual message glyphs and the main message context.
    std::pair{0x0042CB54U, 0x00300530U},
    std::pair{0x0042A278U, 0x0030053CU},
    std::pair{0x00427A3CU, 0x00300548U},
    std::pair{0x0042CBCCU, 0x00300554U}};
constexpr std::array kCmbReturnPcs{
    0x002FADDCU, 0x002FADF4U, 0x002FAEC0U, 0x002FAED8U,
    0x002FEA68U, 0x002FEA74U, 0x003FE408U, 0x003FE414U,
    0x003FE430U, 0x003FE43CU,
};

struct AtmosphereScopeDescriptor {
    uint32_t EntryPc;
    std::span<const uint32_t> ReturnPcs;
};

constexpr std::array kObjectKankyoReturnPcs{kOot3dActorDrawCallbackReturn};
constexpr std::array kSceneRendererReturnPcs{
    0x002E2B8CU, 0x002E2BB8U, 0x0045D3E8U};
constexpr std::array kGameplayFadeReturnPcs{0x002E2BD0U};
constexpr std::array kRandomizedOverlayReturnPcs{0x002E2BDCU};
constexpr std::array kRandomizedModelsReturnPcs{0x002E2BE8U};
constexpr std::array kPrecipitationReturnPcs{0x002E2CD0U};
constexpr std::array kCosineOverlayReturnPcs{0x002E2D2CU};
constexpr std::array kProjectedOverlayReturnPcs{0x002E2D34U};
constexpr std::array kNormalizedColorReturnPcs{0x002E2D78U, 0x002E2DA4U};
// Gameplay_Draw builds the native environment queue through the owners above,
// then executes that queue at 0x002E2C00. The same generic executor services
// unrelated queues elsewhere, so only this exact continuation is atmospheric.
constexpr std::array kGameplayAtmosphereQueueReturnPcs{0x002E2C04U};
// The only native xref to this primitive consumer is the kankyo/effect render
// context submit at 0x003FBC58.
constexpr std::array kKankyoEffectPrimitiveReturnPcs{0x003FBC5CU};
constexpr std::array kPrimitivePacketReturnPcs{
    0x003FB984U, 0x003FB998U, 0x004527BCU};

constexpr std::array kAtmosphereScopes{
    AtmosphereScopeDescriptor{kOot3dObjectKankyoDrawEntry,
                              kObjectKankyoReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dSceneRendererSubmitConfiguredModelPassEntry,
        kSceneRendererReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererUpdateGameplayFadeLayersEntry,
        kGameplayFadeReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererUpdateRandomizedOverlayEntry,
        kRandomizedOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawRandomizedTextureStageModelsEntry,
        kRandomizedModelsReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawPrecipitationEffectsEntry,
        kPrecipitationReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawCosineScaledOverlayEntry,
        kCosineOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dGameplayDrawConfiguredProjectedOverlayEntry,
        kProjectedOverlayReturnPcs},
    AtmosphereScopeDescriptor{
        kOot3dEnvironmentRendererSubmitNormalizedColor4Entry,
        kNormalizedColorReturnPcs},
    AtmosphereScopeDescriptor{kOot3dRendererCommandListExecuteEntry,
                              kGameplayAtmosphereQueueReturnPcs},
    AtmosphereScopeDescriptor{kOot3dKankyoEffectPrimitiveDrawEntry,
                              kKankyoEffectPrimitiveReturnPcs},
};

constexpr auto BuildCompositionHookPcs() {
    constexpr size_t count =
        8U + kUiViews.size() * 2U + kCmbReturnPcs.size() +
        (kObjectKankyoReturnPcs.size() + kSceneRendererReturnPcs.size() +
         kGameplayFadeReturnPcs.size() +
         kRandomizedOverlayReturnPcs.size() +
         kRandomizedModelsReturnPcs.size() +
         kPrecipitationReturnPcs.size() + kCosineOverlayReturnPcs.size() +
         kProjectedOverlayReturnPcs.size() +
         kNormalizedColorReturnPcs.size() +
         kGameplayAtmosphereQueueReturnPcs.size() +
         kKankyoEffectPrimitiveReturnPcs.size()) +
        kAtmosphereScopes.size() + 1U + kPrimitivePacketReturnPcs.size() +
        kNativeA32LightingHookPcs.size();
    std::array<uint32_t, count> hooks{};
    size_t cursor = 0;
    for (const auto& [entry, returnPc] : kUiViews) {
        hooks[cursor++] = entry;
        hooks[cursor++] = returnPc;
    }
    hooks[cursor++] = kOot3dCmbRendererSubmitDrawHandleEntry;
    hooks[cursor++] = kOot3dMeshCommandPacketSubmitEntry;
    hooks[cursor++] = kOot3dGameplayDrawEntry;
    hooks[cursor++] = kOot3dModelQueueSubmitEntry;
    hooks[cursor++] = kOot3dModelQueueSetSlotEntry;
    hooks[cursor++] = kOot3dActorDrawEntry;
    hooks[cursor++] = kTemporalActorDestroy;
    hooks[cursor++] = kTemporalActorInit;
    for (const auto pc : kCmbReturnPcs) {
        hooks[cursor++] = pc;
    }
    for (const auto& descriptor : kAtmosphereScopes) {
        hooks[cursor++] = descriptor.EntryPc;
        for (const auto pc : descriptor.ReturnPcs) {
            hooks[cursor++] = pc;
        }
    }
    hooks[cursor++] = kOot3dPicaPrimitivePacketBuilderEntry;
    for (const auto pc : kPrimitivePacketReturnPcs) {
        hooks[cursor++] = pc;
    }
    for (const auto pc : kNativeA32LightingHookPcs) {
        hooks[cursor++] = pc;
    }
    return hooks;
}

constexpr auto kCompositionHookPcs = BuildCompositionHookPcs();

void SetError(std::string* error, std::string_view message) {
    if (error != nullptr) {
        *error = message;
    }
}

Oot3dPicaCompositionAttribution AttributionForCmbPass(uint8_t pass) {
    // CmbRenderer_SubmitDrawHandle stores this byte at renderer+0x15 and uses
    // it to split MSHS meshes at the native +0x0C boundary. Native control
    // flow submits pass 0 before pass 1 throughout the model-list owners.
    return {
        pass == 0U ? Oot3dPicaCompositionLayer::OpaqueWorld
                   : Oot3dPicaCompositionLayer::TransparentWorld,
        Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        kOot3dCmbRendererSubmitDrawHandleEntry,
        pass,
    };
}

Oot3dPicaCompositionAttribution AttributionForAtmosphere(
    uint32_t sourcePc, uint32_t nativeValue) {
    return {
        Oot3dPicaCompositionLayer::Atmosphere,
        Oot3dPicaCompositionProvenance::NativeControlFlow,
        sourcePc,
        nativeValue,
    };
}

const AtmosphereScopeDescriptor* FindAtmosphereScope(uint32_t pc) {
    const auto found = std::find_if(
        kAtmosphereScopes.begin(), kAtmosphereScopes.end(),
        [pc](const auto& descriptor) { return descriptor.EntryPc == pc; });
    return found == kAtmosphereScopes.end() ? nullptr : &*found;
}

bool Contains(std::span<const uint32_t> values, uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

uint32_t AtmosphereNativeValue(
    uint32_t sourcePc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    Oot3dNativePicaCompositionStats& stats) {
    if (sourcePc == kOot3dObjectKankyoDrawEntry) {
        uint16_t selector = std::numeric_limits<uint16_t>::max();
        if (!memory.Read16(state.r[0] + 0x1CU, &selector)) {
            ++stats.AtmosphereSelectorReadFailures;
        }
        return selector;
    }
    if (sourcePc == kOot3dSceneRendererSubmitConfiguredModelPassEntry) {
        // The sky model selector. r3 is the sky blend, which the game steps
        // every few frames as the sky colour changes: a draw parameter, not an
        // identity. Keeping it here made the same sky a new draw at each step,
        // so visual interpolation could not match it and the sky stuttered.
        return state.r[2] & 0xFFFFU;
    }
    return 0U;
}

bool SameAttribution(const Oot3dPicaCompositionAttribution& left,
                     const Oot3dPicaCompositionAttribution& right) {
    return left == right;
}

} // namespace

std::span<const uint32_t> Oot3dNativePicaCompositionHookPcs() noexcept {
    return kCompositionHookPcs;
}

bool Oot3dNativePicaCompositionTracker::RecordPendingSpan(
    Oot3dPicaCommandListCompositionSpan span) {
    // Reject a crossing owner before erasing nested spans. Otherwise an invalid
    // UI range could destroy valid scene attribution even though it is rejected.
    if (span.Attribution.Layer == Oot3dPicaCompositionLayer::Ui) {
        for (const auto& pending : mPendingSpans) {
            const bool overlaps = span.BeginAddress < pending.EndAddress &&
                                  pending.BeginAddress < span.EndAddress;
            const bool contains = span.BeginAddress <= pending.BeginAddress &&
                                  span.EndAddress >= pending.EndAddress;
            if (overlaps && !contains && !SameAttribution(span.Attribution, pending.Attribution)) {
                ++mStats.OverlappingPacketSpans;
                return false;
            }
        }
    }
    for (auto it = mPendingSpans.begin(); it != mPendingSpans.end();) {
        const bool overlaps = span.BeginAddress < it->EndAddress &&
                              it->BeginAddress < span.EndAddress;
        const bool adjacent = span.EndAddress == it->BeginAddress ||
                              it->EndAddress == span.BeginAddress;
        if (span.Attribution.Layer == Oot3dPicaCompositionLayer::Ui &&
            span.BeginAddress <= it->BeginAddress && span.EndAddress >= it->EndAddress) {
            // A model inside a UI view is UI, not opaque world geometry.
            it = mPendingSpans.erase(it);
            continue;
        }
        if (SameAttribution(span.Attribution, it->Attribution) &&
            span.LightBinding == it->LightBinding && (overlaps || adjacent)) {
            span.BeginAddress = std::min(span.BeginAddress, it->BeginAddress);
            span.EndAddress = std::max(span.EndAddress, it->EndAddress);
            it = mPendingSpans.erase(it);
            ++mStats.CoalescedPacketSpans;
            continue;
        }
        if (overlaps) {
            ++mStats.OverlappingPacketSpans;
            return false;
        }
        ++it;
    }
    if (mPendingSpans.size() == MaximumPendingSpans) {
        mPendingSpans.pop_front();
        ++mStats.EvictedPacketSpans;
    }
    mPendingSpans.push_back(std::move(span));
    return true;
}

void Oot3dNativePicaCompositionTracker::ObserveBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    mLighting.ObserveBlockEntry(pc, state, memory);
    if (pc == kTemporalActorDestroy || pc == kTemporalActorInit)
        mActorLifetimes.erase(pc == kTemporalActorDestroy ? state.r[0] : state.r[4]);
    // Actor_Draw(play, actor) invokes actor+140 and resumes at 002D6314.
    // Tag the producer's queue slots; rigid actor meshes are not static world.
    if (pc == kOot3dActorDrawEntry) {
        uint16_t actorId = 0;
        uint64_t identity = 0;
        if (memory.Read16(state.r[1], &actorId))
            identity = (uint64_t{actorId} << 32U) | state.r[1];
        else
            ++mStats.ReadFailures;
        if (identity != 0 && !mActorLifetimes.contains(state.r[1])) {
            if (++mNextTemporalLifetime == 0) ++mNextTemporalLifetime;
            mActorLifetimes.emplace(state.r[1], mNextTemporalLifetime);
        }
        mActorDrawStack.push_back(identity);
    } else if (pc == kOot3dActorDrawCallbackReturn && !mActorDrawStack.empty()) {
        mActorDrawStack.pop_back();
    }
    if (pc == kOot3dGameplayDrawEntry) {
        mProjectedShadowRenderers.clear();
        mQueuedComposition.clear();
        mActorDrawStack.clear();
        mLighting.CaptureSceneLights(state.r[0], memory);
    } else if (pc == kOot3dModelQueueSubmitEntry &&
               (state.r[14] == 0x001D04DCU || state.r[14] == 0x0033E684U ||
                state.r[14] == 0x0033EA3CU)) {
        // Exact BL continuations in ActorShadow_SubmitFootModel,
        // ActorShadow_DrawFeet's floor-aligned helper and ActorShadow_Draw.
        // Observe after their model allocation/selection.
        // Player_DrawFeetShadow (00297E04) delegates to ActorShadow_DrawFeet
        // (001D04F4), whose floor-aligned models queue from helper 0033E450.
        // The queue consumer (002FAD2C) draws model+14/+18 CMB renderers;
        // model+10 is a resource, not a renderer or a lighting context.
        if (state.r[0] <= std::numeric_limits<uint32_t>::max() - 0x18U) {
            for (uint32_t offset : {0x14U, 0x18U}) {
                uint32_t renderer = 0;
                if (memory.Read32(state.r[0] + offset, &renderer) && renderer != 0) {
                    const auto at = std::lower_bound(
                        mProjectedShadowRenderers.begin(), mProjectedShadowRenderers.end(), renderer);
                    if (at == mProjectedShadowRenderers.end() || *at != renderer)
                        mProjectedShadowRenderers.insert(at, renderer);
                }
            }
        }
    }
    if (pc == kOot3dModelQueueSetSlotEntry) {
        // 0031487C stores model r2 in the exact eight-byte queue slot r1.
        // Keep the producer's ownership on that slot, not on the model: the
        // same model may also be submitted by an unrelated world/UI owner.
        const auto at = std::lower_bound(mQueuedComposition.begin(), mQueuedComposition.end(), state.r[1],
            [](const QueuedComposition& entry, uint32_t address) { return entry.SlotAddress < address; });
        if ((mActiveAtmosphereScope || !mActorDrawStack.empty()) && !mActiveUiScope && state.r[2] != 0) {
            auto attribution = mActiveAtmosphereScope
                ? AttributionForAtmosphere(mActiveAtmosphereScope->SourcePc, mActiveAtmosphereScope->NativeValue)
                : Oot3dPicaCompositionAttribution{};
            attribution.NativeActorGeometry = !mActorDrawStack.empty();
            attribution.NativeActorIdentity = mActorDrawStack.empty() ? 0 : mActorDrawStack.back();
            attribution.TemporalIdentity.Lifetime = ActorTemporalLifetime(attribution.NativeActorIdentity);
            attribution.IsolatedLighting = IsTitleLogoActor(attribution.NativeActorIdentity);
            if (at != mQueuedComposition.end() && at->SlotAddress == state.r[1])
                at->Attribution = attribution;
            else
                mQueuedComposition.insert(at, {state.r[1], attribution});
        } else if (at != mQueuedComposition.end() && at->SlotAddress == state.r[1]) {
            mQueuedComposition.erase(at);
        }
        return;
    }
    const auto ui = std::find(kUiViews.begin(), kUiViews.end(), std::pair{pc, state.r[14]});
    if (ui != kUiViews.end() && !mActiveUiScope) {
        ActiveAtmosphereScope scope{};
        scope.SourcePc = pc;
        scope.ReturnPc = state.r[14];
        scope.BeginAddressValid = memory.Read32(kPicaCommandListCursorAddress, &scope.BeginAddress);
        mActiveUiScope = std::move(scope);
        ++mStats.UiScopeEntries;
        return;
    }
    if (mActiveUiScope && pc == mActiveUiScope->ReturnPc) {
        const auto scope = std::move(*mActiveUiScope);
        mActiveUiScope.reset();
        uint32_t end = 0;
        if (scope.BeginAddressValid && memory.Read32(kPicaCommandListCursorAddress, &end) &&
            end > scope.BeginAddress && !(end & 7U) && !(scope.BeginAddress & 7U)) {
            if (RecordPendingSpan({scope.BeginAddress, end,
                    {Oot3dPicaCompositionLayer::Ui, Oot3dPicaCompositionProvenance::NativeUiLifecycle,
                     scope.SourcePc, 0U}})) ++mStats.UiCommandSpans;
        }
        ++mStats.UiScopeExits;
        return;
    }
    if (const auto* descriptor = FindAtmosphereScope(pc);
        descriptor != nullptr) {
        // The shared primitive writer services UI as well as environment draws.
        // Preserve the outer owner instead of relabelling its packets atmosphere.
        if (mActiveUiScope) return;
        if (mActiveAtmosphereScope.has_value()) {
            ++mStats.NestedAtmosphereScopeEntries;
            return;
        }
        if (!Contains(descriptor->ReturnPcs, state.r[14])) {
            if (pc == kOot3dRendererCommandListExecuteEntry) {
                ++mStats.IgnoredUnrelatedCommandListExecutions;
            } else {
                ++mStats.InvalidAtmosphereReturnAddresses;
            }
            return;
        }
        ActiveAtmosphereScope scope{};
        scope.SourcePc = pc;
        scope.ReturnPc = state.r[14];
        scope.NativeValue =
            AtmosphereNativeValue(pc, state, memory, mStats);
        scope.BeginAddressValid = memory.Read32(
            kPicaCommandListCursorAddress, &scope.BeginAddress);
        if (!scope.BeginAddressValid) {
            ++mStats.ReadFailures;
        }
        mActiveAtmosphereScope = std::move(scope);
        ++mStats.AtmosphereScopeEntries;
        return;
    }

    if (mActiveAtmosphereScope.has_value() &&
        pc == mActiveAtmosphereScope->ReturnPc) {
        auto scope = std::move(*mActiveAtmosphereScope);
        mActiveAtmosphereScope.reset();
        uint32_t endAddress = 0U;
        const bool endAddressValid =
            memory.Read32(kPicaCommandListCursorAddress, &endAddress);
        const bool rangeValid =
            scope.BeginAddressValid && endAddressValid &&
            (scope.BeginAddress & 7U) == 0U &&
            (endAddress & 7U) == 0U && endAddress >= scope.BeginAddress;
        if (!endAddressValid) {
            ++mStats.ReadFailures;
        }
        bool directSpanRecorded = false;
        if (rangeValid && endAddress > scope.BeginAddress) {
            Oot3dPicaCommandListCompositionSpan span{
                scope.BeginAddress, endAddress,
                AttributionForAtmosphere(scope.SourcePc,
                                         scope.NativeValue)};
            if (RecordPendingSpan(std::move(span))) {
                ++mStats.DirectAtmosphereSpans;
                mStats.DirectAtmosphereBytes +=
                    endAddress - scope.BeginAddress;
                directSpanRecorded = true;
            }
        }
        if (!rangeValid) {
            ++mStats.InvalidAtmosphereCommandRanges;
        }
        if ((!rangeValid || !directSpanRecorded) &&
            !scope.PacketSpans.empty()) {
            for (auto& span : scope.PacketSpans) {
                if (RecordPendingSpan(std::move(span))) {
                    ++mStats.RecoveredAtmospherePacketSpans;
                }
            }
        }
        ++mStats.AtmosphereScopeExits;
        return;
    }

    if (mActiveCmbReturnPc.has_value() && pc == *mActiveCmbReturnPc) {
        mActiveCmbPass.reset();
        mActiveCmbReturnPc.reset();
        mActiveCmbRenderer = 0;
        ++mStats.CmbPassExits;
        return;
    }

    if (pc == kOot3dCmbRendererSubmitDrawHandleEntry) {
        const uint32_t pass = state.r[1];
        if (pass > 1U || !Contains(kCmbReturnPcs, state.r[14])) {
            mActiveCmbPass.reset();
            mActiveCmbReturnPc.reset();
            mActiveCmbRenderer = 0;
            if (pass > 1U) {
                ++mStats.InvalidCmbPassEntries;
            } else {
                ++mStats.InvalidCmbReturnAddresses;
            }
            return;
        }
        mActiveCmbPass = static_cast<uint8_t>(pass);
        mActiveCmbReturnPc = state.r[14];
        mActiveCmbRenderer = state.r[0];
        mActiveCmbAttribution = AttributionForCmbPass(static_cast<uint8_t>(pass));
        mActiveCmbAttribution.NativeActorGeometry = !mActorDrawStack.empty() && !mActiveUiScope;
        mActiveCmbAttribution.NativeActorIdentity =
            mActiveCmbAttribution.NativeActorGeometry ? mActorDrawStack.back() : 0;
        mActiveCmbAttribution.TemporalIdentity.Lifetime =
            ActorTemporalLifetime(mActiveCmbAttribution.NativeActorIdentity);
        mActiveCmbAttribution.IsolatedLighting =
            mActiveCmbAttribution.NativeActorGeometry &&
            IsTitleLogoActor(mActiveCmbAttribution.NativeActorIdentity);
        // The model-array consumers 002FAD2C, 002FAE10 and 003FE374 keep
        // the array in r8 and its current index in r4 at these CMB calls.
        // 002FEA30 uses a different layout and is deliberately excluded.
        if (state.r[14] != 0x002FEA68U && state.r[14] != 0x002FEA74U) {
            const uint64_t slot = uint64_t{state.r[8]} + uint64_t{state.r[4]} * 8U;
            const auto at = std::lower_bound(mQueuedComposition.begin(), mQueuedComposition.end(), slot,
                [](const QueuedComposition& entry, uint64_t address) { return entry.SlotAddress < address; });
            if (at != mQueuedComposition.end() && at->SlotAddress == slot) {
                if (at->Attribution.Layer != Oot3dPicaCompositionLayer::Unknown)
                    mActiveCmbAttribution = at->Attribution;
                else {
                    mActiveCmbAttribution.NativeActorGeometry = at->Attribution.NativeActorGeometry;
                    mActiveCmbAttribution.NativeActorIdentity = at->Attribution.NativeActorIdentity;
                    mActiveCmbAttribution.TemporalIdentity.Lifetime = at->Attribution.TemporalIdentity.Lifetime;
                    mActiveCmbAttribution.IsolatedLighting = at->Attribution.IsolatedLighting;
                }
            }
        }
        if (pass == 0U) {
            ++mStats.CmbOpaquePassEntries;
        } else {
            ++mStats.CmbTransparentPassEntries;
        }
        return;
    }

    if (mActivePrimitivePacket.has_value() &&
        pc == mActivePrimitivePacket->ReturnPc) {
        auto packet = std::move(*mActivePrimitivePacket);
        mActivePrimitivePacket.reset();
        uint32_t endAddress = 0U;
        const bool endAddressValid =
            packet.ContextAddress <=
                std::numeric_limits<uint32_t>::max() - 8U &&
            memory.Read32(packet.ContextAddress + 8U, &endAddress);
        if (!endAddressValid) {
            ++mStats.ReadFailures;
        }
        const bool rangeValid =
            packet.BeginAddressValid && endAddressValid &&
            (packet.BeginAddress & 7U) == 0U &&
            (endAddress & 7U) == 0U && endAddress > packet.BeginAddress;
        if (!rangeValid) {
            ++mStats.InvalidPrimitivePacketRanges;
        } else if (RecordPendingSpan(
                       {packet.BeginAddress, endAddress,
                        packet.Attribution, packet.LightBinding})) {
            ++mStats.PrimitivePacketSpans;
            mStats.PrimitivePacketBytes +=
                endAddress - packet.BeginAddress;
        }
        ++mStats.PrimitivePacketExits;
        return;
    }
    if (Contains(kPrimitivePacketReturnPcs, pc)) {
        ++mStats.UnmatchedPrimitivePacketExits;
        return;
    }
    if (pc == kOot3dPicaPrimitivePacketBuilderEntry) {
        if (mActivePrimitivePacket.has_value()) {
            ++mStats.NestedPrimitivePacketEntries;
            return;
        }
        if (!Contains(kPrimitivePacketReturnPcs, state.r[14])) {
            ++mStats.InvalidPrimitivePacketReturnAddresses;
            return;
        }
        ActivePrimitivePacket packet{};
        packet.ReturnPc = state.r[14];
        packet.BeginAddressValid =
            memory.Read32(state.r[0], &packet.ContextAddress) &&
            packet.ContextAddress <=
                std::numeric_limits<uint32_t>::max() - 8U &&
            memory.Read32(packet.ContextAddress + 8U,
                          &packet.BeginAddress);
        if (!packet.BeginAddressValid) {
            ++mStats.ReadFailures;
        }
        if (mActiveUiScope) {
            packet.Attribution = {
                Oot3dPicaCompositionLayer::Ui,
                Oot3dPicaCompositionProvenance::NativeUiLifecycle,
                mActiveUiScope->SourcePc, 0U};
        } else if (mActiveAtmosphereScope.has_value()) {
            packet.Attribution = AttributionForAtmosphere(
                mActiveAtmosphereScope->SourcePc,
                mActiveAtmosphereScope->NativeValue);
            packet.LightBinding.WorldToView = NativeA32LightingProbe::CaptureWorldToView(memory);
        } else if (mActiveCmbPass.has_value()) {
            packet.Attribution = mActiveCmbAttribution;
            packet.Attribution.NativeProjectedShadow = std::binary_search(
                mProjectedShadowRenderers.begin(), mProjectedShadowRenderers.end(), mActiveCmbRenderer);
            packet.LightBinding = mLighting.CaptureRenderer(mActiveCmbRenderer, memory);
        } else {
            packet.Attribution = {
                Oot3dPicaCompositionLayer::Unknown,
                Oot3dPicaCompositionProvenance::NativeControlFlow,
                kOot3dPicaPrimitivePacketBuilderEntry,
                state.r[14],
            };
        }
        mActivePrimitivePacket = std::move(packet);
        ++mStats.PrimitivePacketEntries;
        return;
    }
    if (pc != kOot3dMeshCommandPacketSubmitEntry) {
        return;
    }

    ++mStats.MeshPacketEntries;
    uint32_t byteCount = 0U;
    uint32_t cursor = 0U;
    if (!memory.Read32(state.r[0] + kMeshPacketByteCountOffset,
                       &byteCount) ||
        !memory.Read32(kPicaCommandListCursorAddress, &cursor)) {
        ++mStats.ReadFailures;
        return;
    }
    const int32_t signedByteCount = static_cast<int32_t>(byteCount);
    if (signedByteCount <= 0) {
        return;
    }
    const uint64_t end = static_cast<uint64_t>(cursor) + byteCount;
    if ((cursor & 7U) != 0U || (byteCount & 7U) != 0U ||
        end > std::numeric_limits<uint32_t>::max()) {
        ++mStats.InvalidPacketSpans;
        return;
    }
    if (!mActiveAtmosphereScope.has_value() &&
        !mActiveCmbPass.has_value()) {
        ++mStats.UnattributedPacketSpans;
        return;
    }

    Oot3dPicaCommandListCompositionSpan span{
        cursor, static_cast<uint32_t>(end), {}};
    if (mActiveAtmosphereScope.has_value()) {
        span.Attribution = AttributionForAtmosphere(
            mActiveAtmosphereScope->SourcePc,
            mActiveAtmosphereScope->NativeValue);
        span.LightBinding.WorldToView = NativeA32LightingProbe::CaptureWorldToView(memory);
        ++mStats.AtmospherePacketSpans;
        ++mStats.RecordedPacketSpans;
        mActiveAtmosphereScope->PacketSpans.push_back(std::move(span));
        return;
    } else {
        span.Attribution = mActiveCmbAttribution;
        // 0030F4D0 r0 is a CMB renderer INSTANCE, while 00466E2C r0 is
        // its mesh command-packet descriptor. The descriptor selects two
        // alternating backing command buffers; neither the active buffer nor
        // the destination cursor is an instance identity. The frontend adds
        // the trigger's offset inside this packet, distinguishing its draws.
        if (!mActiveUiScope && mActiveCmbRenderer != 0 && state.r[0] != 0 &&
            (!span.Attribution.NativeActorGeometry || span.Attribution.NativeActorIdentity != 0)) {
            span.Attribution.TemporalIdentity = {
                span.Attribution.NativeActorIdentity, mActiveCmbRenderer, state.r[0],
                uint64_t{*mActiveCmbPass} << 32U,
                span.Attribution.NativeActorGeometry ? span.Attribution.TemporalIdentity.Lifetime
                                                     : mWorldTemporalLifetime};
            span.Attribution.TemporalPacketBeginAddress = cursor;
        }
        span.Attribution.NativeProjectedShadow = std::binary_search(
            mProjectedShadowRenderers.begin(), mProjectedShadowRenderers.end(), mActiveCmbRenderer);
        if (!mActiveUiScope) {
            span.LightBinding = mLighting.CaptureRenderer(mActiveCmbRenderer, memory);
        }
    }
    if (RecordPendingSpan(std::move(span))) {
        ++mStats.RecordedPacketSpans;
    }
}

bool Oot3dNativePicaCompositionTracker::TakeCommandListCompositionSpans(
    uint32_t commandListAddress, uint32_t commandListSize,
    std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
    std::string* error) {
    spans.clear();
    const uint64_t end64 =
        static_cast<uint64_t>(commandListAddress) + commandListSize;
    if ((commandListAddress & 3U) != 0U ||
        (commandListSize & 3U) != 0U ||
        end64 > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "submitted command-list range is invalid");
        return false;
    }
    const uint32_t commandListEnd = static_cast<uint32_t>(end64);
    for (auto it = mPendingSpans.begin(); it != mPendingSpans.end();) {
        const bool intersects = it->BeginAddress < commandListEnd &&
                                commandListAddress < it->EndAddress;
        if (!intersects) {
            ++it;
            continue;
        }
        const bool contained = it->BeginAddress >= commandListAddress &&
                               it->EndAddress <= commandListEnd;
        if (contained) {
            spans.push_back(*it);
            ++mStats.ConsumedPacketSpans;
        } else {
            ++mStats.PartiallyOverlappingSubmissions;
        }
        it = mPendingSpans.erase(it);
    }
    std::sort(spans.begin(), spans.end(),
              [](const auto& left, const auto& right) {
                  return left.BeginAddress < right.BeginAddress;
              });
    return true;
}

void Oot3dNativePicaCompositionTracker::Reset() noexcept {
    if (++mNextTemporalLifetime == 0) ++mNextTemporalLifetime;
    mWorldTemporalLifetime = mNextTemporalLifetime;
    mActorLifetimes.clear();
    mActiveUiScope.reset();
    mActiveCmbPass.reset();
    mActiveCmbReturnPc.reset();
    mActiveCmbRenderer = 0;
    mProjectedShadowRenderers.clear();
    mQueuedComposition.clear();
    mActorDrawStack.clear();
    mActiveCmbAttribution = {};
    mLighting.Reset();
    mActiveAtmosphereScope.reset();
    mActivePrimitivePacket.reset();
    mPendingSpans.clear();
    mStats = {};
}

const Oot3dNativePicaCompositionStats&
Oot3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
