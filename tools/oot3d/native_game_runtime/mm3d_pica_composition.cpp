#include "mm3d_pica_composition.h"

#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>

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
    // A 2D layout, from 0x001267E8 after it binds the layout's list.
    ScopeDescriptor{kMm3dLayoutDrawEntry, 0x001269DCU,
                    Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle},
};

constexpr auto BuildHookPcs() {
    std::array<uint32_t, kScopes.size() * 2U> pcs{};
    size_t cursor = 0;
    for (const auto& scope : kScopes) {
        pcs[cursor++] = scope.EntryPc;
        pcs[cursor++] = scope.ReturnPc;
    }
    return pcs;
}
constexpr auto kHookPcs = BuildHookPcs();

// Spans older than this many scene frames are dropped: the GPU runs a list
// one guest frame after it is built.
constexpr uint64_t kSpanLifetimeSceneFrames = 2U;

} // namespace

std::span<const uint32_t> Mm3dNativePicaCompositionHookPcs() noexcept {
    return kHookPcs;
}

Mm3dNativePicaCompositionTracker::Event
Mm3dNativePicaCompositionTracker::ObserveBlockEntry(
    uint32_t pc, const oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory) {
    if (mActive.has_value() && pc == mActive->ReturnPc) {
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
            Record(scope.BeginAddress, end, scope.Attribution);
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
    mActive = ActiveScope{scope->ReturnPc, begin,
                          {scope->Layer, scope->Provenance, scope->EntryPc,
                           scope->ReturnPc}};
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

void Mm3dNativePicaCompositionTracker::Record(
    uint32_t begin, uint32_t end,
    const Oot3dPicaCompositionAttribution& attribution) {
    // A later frame writing the same bytes replaces what was recorded there.
    const auto overlapped = std::remove_if(
        mSpans.begin(), mSpans.end(), [begin, end](const RecordedSpan& span) {
            return span.Span.BeginAddress < end && begin < span.Span.EndAddress;
        });
    mStats.ReplacedSpans += static_cast<uint64_t>(mSpans.end() - overlapped);
    mSpans.erase(overlapped, mSpans.end());
    const auto at = std::lower_bound(
        mSpans.begin(), mSpans.end(), begin,
        [](const RecordedSpan& span, uint32_t address) {
            return span.Span.BeginAddress < address;
        });
    mSpans.insert(at, {{begin, end, attribution}, mStats.SceneFrames});
    ++mStats.RecordedSpans;
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
    mStats = {};
}

const Mm3dNativePicaCompositionStats&
Mm3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
