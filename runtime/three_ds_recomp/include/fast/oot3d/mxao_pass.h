#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/pica_surface_coordinates.h"
#include "fast/oot3d/scene_view_runtime.h"
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <string>

namespace Fast::Oot3d {
class NriInteropContext;
struct NriEffectGraphTransientImageBinding;
struct TemporalViewState;

// The declared motion vectors a temporal MXAO reprojects its history with, at
// their own extent.
struct MxaoMotionInput {
    VkImage Image = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
};

// One frame of temporal MXAO: the view's current and previous transforms and
// the rendered frame, which must directly follow the history's own.
struct MxaoTemporalFrame {
    const TemporalViewState* View = nullptr;
    uint64_t FrameId = 0U;
};

// iMMERSE MXAO provider: TriAevum's side of the iMMERSE module. The module is
// a separate shared library under its own licence (fast/immerse/LICENSE.md),
// loaded through triaevum/immerse_module.h; this pass gives it the renderer's
// services and is unavailable when the library is not installed. MXAO
// reads the same native depth and normal guide as GTAO and publishes the same
// R32F ambient visibility. Without motion vectors there is no history, so a
// frame depends only on its own inputs; with them the per-pixel occlusion
// accumulates over frames, with a rotating jitter phase, before the filter.
class MxaoPass final {
  public:
    MxaoPass();
    ~MxaoPass();
    MxaoPass(const MxaoPass&) = delete;
    MxaoPass& operator=(const MxaoPass&) = delete;
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    // `geometry` is the geometry guide (R32G32B32A32_UINT), or a cleared
    // image of that format when the face normals are not read.
    bool Configure(const NriEffectGraphTransientImageBinding& output, VkImage depth, VkFormat depthFormat,
                   VkImage normal, VkFormat normalFormat, VkImage geometry,
                   const MxaoMotionInput* motion = nullptr);
    bool Execute(uint32_t frameSlot, uint64_t frameId, const PerspectiveViewState& view, bool wBuffer, const EffectsSettings& settings,
                 const EffectPassBarrierPlan& barriers, PicaSurfaceCoordinates coordinates,
                 const MxaoTemporalFrame* temporal = nullptr);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    // The last execution accumulated over a reprojected history.
    [[nodiscard]] bool LastHistoryValid() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d
#endif
