#include "mm3d_pica_composition.h"

#include "mm3d_title_contracts.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
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

bool WorldLayer(Oot3dPicaCompositionLayer layer) {
    return layer == Oot3dPicaCompositionLayer::OpaqueWorld ||
           layer == Oot3dPicaCompositionLayer::TransparentWorld;
}

// Column-major world-to-view from the View's Mtx34 rows.
std::optional<std::array<float, 16>> ReadWorldToView(
    oot3d::recomp::a32::MemoryBus& memory, uint32_t view) {
    std::array<float, 16> worldToView{};
    worldToView[15] = 1.0F;
    for (uint32_t row = 0; row < 3U; ++row) {
        for (uint32_t column = 0; column < 4U; ++column) {
            uint32_t word = 0;
            if (!memory.Read32(view + kMm3dViewWorldToViewOffset + (row * 4U + column) * 4U, &word))
                return std::nullopt;
            const float value = std::bit_cast<float>(word);
            if (!std::isfinite(value)) return std::nullopt;
            worldToView[column * 4U + row] = value;
        }
    }
    return worldToView;
}

// A directional LightInfo record of the environment.
bool ReadDirectionalLight(oot3d::recomp::a32::MemoryBus& memory, uint32_t info,
                          Fast::Renderer3ds::PicaSceneDirectionalLight& light) {
    uint8_t kind = 0;
    if (!memory.Read8(info, &kind) || kind != 1U) return false;
    for (uint32_t axis = 0; axis < 3U; ++axis) {
        uint8_t direction = 0, color = 0;
        if (!memory.Read8(info + 4U + axis, &direction) || !memory.Read8(info + 7U + axis, &color))
            return false;
        light.DirectionTowardSource[axis] = static_cast<float>(std::bit_cast<int8_t>(direction)) / 127.0F;
        light.Color[axis] = static_cast<float>(color) / 255.0F;
    }
    light.SourceIdentity = info;
    return true;
}

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
    mActive = ActiveScope{scope->ReturnPc, begin,
                          {scope->Layer, scope->Provenance, scope->EntryPc,
                           scope->ReturnPc},
                          WorldLayer(scope->Layer)
                              ? mSceneLighting
                              : Fast::Renderer3ds::PicaNativeLightBinding{}};
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
    using namespace Fast::Renderer3ds;
    PicaNativeLightBinding binding;
    uint32_t view = 0;
    if (memory.Read32(kMm3dCurrentViewPointer, &view) && view != 0U &&
        view > kMm3dPlayViewOffset &&
        view <= std::numeric_limits<uint32_t>::max() - 0x200U) {
        binding.WorldToView = ReadWorldToView(memory, view);
        // Lights still linked in the play state's LightContext: a View that
        // is not a play state's links none of these records.
        const uint32_t play = view - kMm3dPlayViewOffset;
        PicaSceneDirectionalLights directionals;
        uint32_t node = 0;
        if (play <= std::numeric_limits<uint32_t>::max() - kMm3dPlayEnvironmentLight2Offset - 0x10U &&
            memory.Read32(play + kMm3dPlayLightListHeadOffset, &node)) {
            for (uint32_t visited = 0; node != 0U && visited < 32U; ++visited) {
                uint32_t info = 0, next = 0;
                if (node > std::numeric_limits<uint32_t>::max() - 12U ||
                    !memory.Read32(node, &info) || !memory.Read32(node + 8U, &next))
                    break;
                PicaSceneDirectionalLight light;
                if ((info == play + kMm3dPlayEnvironmentLight1Offset ||
                     info == play + kMm3dPlayEnvironmentLight2Offset) &&
                    directionals.Count < directionals.Lights.size() &&
                    ReadDirectionalLight(memory, info, light))
                    directionals.Lights[directionals.Count++] = light;
                node = next;
            }
        }
        // Light 1 first, as the environment orders them.
        std::sort(directionals.Lights.begin(), directionals.Lights.begin() + directionals.Count,
                  [](const auto& a, const auto& b) { return a.SourceIdentity < b.SourceIdentity; });
        mStats.SceneDirectionalLights = directionals.Count;
        if (directionals.Count == 0U) {
            mSceneDirectionals.reset();
        } else if (!mSceneDirectionals || *mSceneDirectionals != directionals) {
            mSceneDirectionals = std::make_shared<const PicaSceneDirectionalLights>(directionals);
        }
        binding.SceneDirectionals = mSceneDirectionals;
    }
    if (binding.WorldToView.has_value()) {
        ++mStats.LightBindings;
    }
    mSceneLighting = std::move(binding);
}

void Mm3dNativePicaCompositionTracker::Record(
    uint32_t begin, uint32_t end,
    const Oot3dPicaCompositionAttribution& attribution,
    const Fast::Renderer3ds::PicaNativeLightBinding& lightBinding) {
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
    mSpans.insert(at, {{begin, end, attribution, lightBinding}, mStats.SceneFrames});
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
    mSceneLighting = {};
    mSceneDirectionals.reset();
    mStats = {};
}

const Mm3dNativePicaCompositionStats&
Mm3dNativePicaCompositionTracker::Stats() const noexcept {
    return mStats;
}

} // namespace Oot3dNativeGame
