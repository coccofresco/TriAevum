#pragma once

#include "oot3d_native_pica_frontend.h"
#include "mm3d_native_a32_lighting.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
struct GuestState;
struct Registry;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

// Majora's Mask 3D's native draw passes (code.bin 6eec27b1..., v1.1),
// recovered by tracing one frame's function entries against the command
// list cursor and the PICA draws they produced, and read against Play_Draw in
// the N64 decompilation (zeldaret/mm src/code/z_play.c):
//
//   0x001109F8 draws a frame. It binds command list 9 and calls the sky
//   (0x005B51D0, Skybox_Draw's place), binds list 7 and calls the scene,
//   0x0011EF14, then 0x0011EF60, then the 2D layers through 0x0011EFAC.
//   0x0011EF14 visits the scene's two render lists with 0x005EF054: the opaque
//   list (scene+4, returning to 0x0011EF3C) and the translucent list
//   (scene+4+0x408, returning to 0x0011EF58). 0x0011EF60 visits the same two
//   lists again with another visitor (returning to 0x0011EF88 and
//   0x0011EFA4): blended draws without depth writes, after translucency.
//   0x0011EFAC visits the 2D scene (scene+8) the same way (0x0011EFD4,
//   0x0011EFF4). 0x001267E8 draws one 2D layout queue (r1): for each
//   enqueued owner it calls 0x00126294 (owner in r1; returning to 0x00126998
//   or, in its second loop, 0x00126A64), then 0x001262E0 (returning to
//   0x001269DC, or 0x00126AA8) finishes the queue. HUD, menus and dialogue
//   are these layouts. After the 2D scene pass the frame draw also draws
//   the quad manager's (0x00175A88) sprite queues 4 and 5 with 0x0012C6A8
//   (manager, queue), returning to 0x00110D88 and 0x00110DB8: HUD sprites,
//   among them the map and radar quads mm3d-dekui moves to the top screen.
//
// Each pass writes its commands through the cursor at 0x006A3A88 into the
// bound list; prebuilt model buffers are called from there through
// GPUREG_CMDBUF_JUMP, and the frontend attributes their draws to the call.
inline constexpr uint32_t kMm3dCommandListCursorAddress = 0x006A3A88U;
inline constexpr uint32_t kMm3dRenderListVisitEntry = 0x005EF054U;
inline constexpr uint32_t kMm3dSkyboxDrawEntry = 0x005B51D0U;
inline constexpr uint32_t kMm3dLayoutDrawEntry = 0x001262E0U;
inline constexpr uint32_t kMm3dLayoutOwnerDrawEntry = 0x00126294U;
inline constexpr uint32_t kMm3dQuadQueueDrawEntry = 0x0012C6A8U;

std::span<const uint32_t> Mm3dNativePicaCompositionHookPcs() noexcept;

struct Mm3dNativePicaCompositionStats {
    uint64_t ScopeEntries = 0;
    uint64_t ScopeExits = 0;
    uint64_t RecordedSpans = 0;
    uint64_t EmptyScopes = 0;
    uint64_t InvalidRanges = 0;
    uint64_t NestedScopeEntries = 0;
    uint64_t ReplacedSpans = 0;
    uint64_t ExpiredSpans = 0;
    uint64_t SceneFrames = 0;
    uint64_t ReadFailures = 0;
    // Scene frames whose world passes carry the camera's world-to-view, and
    // the environment's directional lights the last of them carried.
    uint64_t LightBindings = 0;
    uint32_t SceneDirectionalLights = 0;
    uint64_t TemporalModelSpans = 0;
    uint64_t IsolatedModelSpans = 0;
    uint64_t AtmosphereModelSpans = 0;
    uint64_t LifecycleInvalidations = 0;
};

// Records the command-list byte range each native pass writes, with its
// layer, and with the world passes, the scene's lighting: the current View's
// world-to-view and the environment's directional lights still linked in the
// play state's LightContext (mm3d_title_contracts.h). A list is built one guest frame before the GPU runs it, and the GPU
// reaches it through jumps from the submitted list, so the recorded ranges
// are offered with every submission until a later frame writes the same
// bytes again or they are two scene frames old.
class Mm3dNativePicaCompositionTracker final
    : public Oot3dPicaCommandListCompositionProvider {
  public:
    enum class Event : uint8_t { None, OpaqueWorldBegins };

    // Called after mount/registry verification; false fails adapter setup.
    bool VerifyHooks(oot3d::recomp::a32::MemoryBus& memory,
                     const oot3d::recomp::a32::Registry* registry = nullptr);
    void SetSupplementalPublication(bool points, bool water) noexcept {
        mLighting.SetSupplementalPublication(points, water);
    }
    Event ObserveBlockEntry(uint32_t pc,
                            const oot3d::recomp::a32::GuestState& state,
                            oot3d::recomp::a32::MemoryBus& memory);

    bool TakeCommandListCompositionSpans(
        uint32_t commandListAddress, uint32_t commandListSize,
        std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
        std::string* error = nullptr) override;

    void Reset() noexcept;
    [[nodiscard]] const Mm3dNativePicaCompositionStats& Stats() const noexcept;
    // The environment group latched for this play visit (Unknown = nullopt).
    [[nodiscard]] std::optional<SceneEnvironmentGroup> EnvironmentGroup(uint32_t play, uint16_t scene) const noexcept {
        return mLighting.EnvironmentGroup(play, scene);
    }

  private:
    struct QueueOwner {
        uint32_t Model = 0; uint64_t Generation = 0;
        uint64_t Actor = 0; bool Shadow = false;
        uint32_t List = 0;
        uint64_t Lifetime = 0;
        bool Isolated = false, Atmosphere = false;
    };
    struct ActorLifetime { uint64_t Identity = 0, Lifetime = 0; };
    struct ModelLifetime { uint64_t Owner = 0, OwnerEpoch = 0, Lifetime = 0; };
    struct ModelSubmit { uint32_t Model = 0, ReturnPc = 0; bool Shadow = false; };
    struct OverlaySubmit { uint32_t Model = 0, List = 0, ReturnPc = 0; };
    struct ModelScope {
        uint32_t ReturnPc = 0, Slot = 0, Begin = 0;
        Oot3dPicaCompositionAttribution Attribution;
        Fast::Renderer3ds::PicaNativeLightBinding Lighting;
    };
    struct ActiveScope {
        uint32_t ReturnPc = 0;
        uint32_t BeginAddress = 0;
        Oot3dPicaCompositionAttribution Attribution;
        Fast::Renderer3ds::PicaNativeLightBinding LightBinding;
    };
    struct RecordedSpan {
        Oot3dPicaCommandListCompositionSpan Span;
        uint64_t SceneFrame = 0;
    };

    void Record(uint32_t begin, uint32_t end,
                const Oot3dPicaCompositionAttribution& attribution,
                const Fast::Renderer3ds::PicaNativeLightBinding& lightBinding);
    void CaptureSceneLighting(oot3d::recomp::a32::MemoryBus& memory);

    uint64_t NextLifetime() noexcept;
    void InvalidateActor(uint32_t actor);
    bool mHooksVerified = false, mFineSuppressed = false;
    uint64_t mNextLifetime = 0;
    uint32_t mPlay = 0, mPlayFrame = 0;
    uint16_t mScene = 0;
    std::unordered_map<uint32_t, ActorLifetime> mActorLifetimes;
    std::unordered_map<uint32_t, ModelLifetime> mModelLifetimes;
    uint64_t mQueueGeneration = 0;
    std::vector<uint64_t> mActorDrawStack;
    std::unordered_map<uint32_t, QueueOwner> mQueueOwners;
    std::optional<ModelSubmit> mModelSubmit;
    std::optional<OverlaySubmit> mOverlaySubmit;
    std::optional<ModelScope> mModelScope;
    Mm3dNativeA32LightingProbe mLighting;
    std::optional<ActiveScope> mActive;
    // The scene frame's lighting, captured when its opaque pass begins.
    Fast::Renderer3ds::PicaNativeLightBinding mSceneLighting;
    std::vector<RecordedSpan> mSpans; // sorted by BeginAddress, disjoint
    Mm3dNativePicaCompositionStats mStats;
};

} // namespace Oot3dNativeGame
