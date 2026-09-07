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
        2U + kCmbReturnPcs.size() +
        (kObjectKankyoReturnPcs.size() + kSceneRendererReturnPcs.size() +
         kGameplayFadeReturnPcs.size() +
         kRandomizedOverlayReturnPcs.size() +
         kRandomizedModelsReturnPcs.size() +
         kPrecipitationReturnPcs.size() + kCosineOverlayReturnPcs.size() +
         kProjectedOverlayReturnPcs.size() +
         kNormalizedColorReturnPcs.size() +
         kGameplayAtmosphereQueueReturnPcs.size() +
         kKankyoEffectPrimitiveReturnPcs.size()) +
        kAtmosphereScopes.size() + 1U + kPrimitivePacketReturnPcs.size();
    std::array<uint32_t, count> hooks{};
    size_t cursor = 0;
    hooks[cursor++] = kOot3dCmbRendererSubmitDrawHandleEntry;
    hooks[cursor++] = kOot3dMeshCommandPacketSubmitEntry;
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
        return (state.r[2] & 0xFFFFU) | ((state.r[3] & 0xFFFFU) << 16U);
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
    for (auto it = mPendingSpans.begin(); it != mPendingSpans.end();) {
        const bool overlaps = span.BeginAddress < it->EndAddress &&
                              it->BeginAddress < span.EndAddress;
        const bool adjacent = span.EndAddress == it->BeginAddress ||
                              it->EndAddress == span.BeginAddress;
        if (SameAttribution(span.Attribution, it->Attribution) &&
            (overlaps || adjacent)) {
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
    if (const auto* descriptor = FindAtmosphereScope(pc);
        descriptor != nullptr) {
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
        ++mStats.CmbPassExits;
        return;
    }

    if (pc == kOot3dCmbRendererSubmitDrawHandleEntry) {
        const uint32_t pass = state.r[1];
        if (pass > 1U || !Contains(kCmbReturnPcs, state.r[14])) {
            mActiveCmbPass.reset();
            mActiveCmbReturnPc.reset();
            if (pass > 1U) {
                ++mStats.InvalidCmbPassEntries;
            } else {
                ++mStats.InvalidCmbReturnAddresses;
            }
            return;
        }
        mActiveCmbPass = static_cast<uint8_t>(pass);
        mActiveCmbReturnPc = state.r[14];
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
                        packet.Attribution})) {
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
        if (mActiveAtmosphereScope.has_value()) {
            packet.Attribution = AttributionForAtmosphere(
                mActiveAtmosphereScope->SourcePc,
                mActiveAtmosphereScope->NativeValue);
        } else if (mActiveCmbPass.has_value()) {
            packet.Attribution = AttributionForCmbPass(*mActiveCmbPass);
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
        ++mStats.AtmospherePacketSpans;
        ++mStats.RecordedPacketSpans;
        mActiveAtmosphereScope->PacketSpans.push_back(std::move(span));
        return;
    } else {
        span.Attribution = AttributionForCmbPass(*mActiveCmbPass);
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
    mActiveCmbPass.reset();
    mActiveCmbReturnPc.reset();
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
