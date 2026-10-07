#pragma once

#include "oot3d_native_pica_frontend.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oot3d::recomp::a32 {
class MemoryBus;
struct GuestState;
} // namespace oot3d::recomp::a32

namespace Oot3dNativeGame {

// Majora's Mask 3D's native draw passes (code.bin 6eec27b1..., v1.0),
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
//   0x0011EFF4). 0x001262E0, called from 0x001267E8 (returning to
//   0x001269DC) after it binds a layout list, draws a 2D layout.
//
// Each pass writes its commands through the cursor at 0x006A3A88 into the
// bound list; prebuilt model buffers are called from there through
// GPUREG_CMDBUF_JUMP, and the frontend attributes their draws to the call.
inline constexpr uint32_t kMm3dCommandListCursorAddress = 0x006A3A88U;
inline constexpr uint32_t kMm3dRenderListVisitEntry = 0x005EF054U;
inline constexpr uint32_t kMm3dSkyboxDrawEntry = 0x005B51D0U;
inline constexpr uint32_t kMm3dLayoutDrawEntry = 0x001262E0U;

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
};

// Records the command-list byte range each native pass writes, with its
// layer. A list is built one guest frame before the GPU runs it, and the GPU
// reaches it through jumps from the submitted list, so the recorded ranges
// are offered with every submission until a later frame writes the same
// bytes again or they are two scene frames old.
class Mm3dNativePicaCompositionTracker final
    : public Oot3dPicaCommandListCompositionProvider {
  public:
    enum class Event : uint8_t { None, OpaqueWorldBegins };

    Event ObserveBlockEntry(uint32_t pc,
                            const oot3d::recomp::a32::GuestState& state,
                            oot3d::recomp::a32::MemoryBus& memory);

    bool TakeCommandListCompositionSpans(
        uint32_t commandListAddress, uint32_t commandListSize,
        std::vector<Oot3dPicaCommandListCompositionSpan>& spans,
        std::string* error = nullptr) override;

    void Reset() noexcept;
    [[nodiscard]] const Mm3dNativePicaCompositionStats& Stats() const noexcept;

  private:
    struct ActiveScope {
        uint32_t ReturnPc = 0;
        uint32_t BeginAddress = 0;
        Oot3dPicaCompositionAttribution Attribution;
    };
    struct RecordedSpan {
        Oot3dPicaCommandListCompositionSpan Span;
        uint64_t SceneFrame = 0;
    };

    void Record(uint32_t begin, uint32_t end,
                const Oot3dPicaCompositionAttribution& attribution);

    std::optional<ActiveScope> mActive;
    std::vector<RecordedSpan> mSpans; // sorted by BeginAddress, disjoint
    Mm3dNativePicaCompositionStats mStats;
};

} // namespace Oot3dNativeGame
