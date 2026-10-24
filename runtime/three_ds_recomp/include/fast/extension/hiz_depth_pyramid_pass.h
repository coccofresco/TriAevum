#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/hiz_depth_pyramid.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class HiZDepthPyramidPass final {
  public:
    HiZDepthPyramidPass();
    ~HiZDepthPyramidPass();
    HiZDepthPyramidPass(const HiZDepthPyramidPass&) = delete;
    HiZDepthPyramidPass& operator=(const HiZDepthPyramidPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output,
                   VkImage depthImage, VkFormat depthFormat);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 float nearPlane,
                 float farPlane, DepthConvention convention,
                 const EffectPassBarrierPlan& barrierPlan);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] uint32_t MipCount() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
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
