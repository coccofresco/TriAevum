#pragma once
#include "fast/extension/pica_surface_coordinates.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/hiz_depth_pyramid.h"
#include "fast/extension/scene_view_runtime.h"
#include "fast/extension/temporal_history_manager.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class MotionVectorPass final {
  public:
    MotionVectorPass();
    ~MotionVectorPass();
    MotionVectorPass(const MotionVectorPass&) = delete;
    MotionVectorPass& operator=(const MotionVectorPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& motionOutput,
                   const NriEffectGraphTransientImageBinding& reactiveOutput,
                   VkImage depthImage, VkFormat depthFormat,
                   VkImage materialGuideImage, VkFormat materialGuideFormat,
                   VkImage rigidMotionGuideImage,
                   VkFormat rigidMotionGuideFormat);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 const TemporalViewState& temporal,
                 const PerspectiveViewState& view,
                 DepthConvention convention,
                 const EffectPassBarrierPlan& barrierPlan,
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] VkImage ReactiveImage() const;
    [[nodiscard]] VkImageView ReactiveView() const;
    [[nodiscard]] bool OutputsOwnedByNri() const;
    [[nodiscard]] bool ComputeOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension
#endif
