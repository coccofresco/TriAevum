#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/linear_scene_color.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

// Converts the immutable packed PICA display snapshot to the renderer's
// logical RGBA16F working domain. The original UNORM target is never changed.
class LinearSceneColorPass final {
  public:
    LinearSceneColorPass();
    ~LinearSceneColorPass();
    LinearSceneColorPass(const LinearSceneColorPass&) = delete;
    LinearSceneColorPass& operator=(const LinearSceneColorPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 VkImage sourceImage, VkFormat sourceFormat,
                 uint32_t sourceWidth, uint32_t sourceHeight,
                 SceneColorEncoding sourceEncoding,
                 const EffectPassBarrierPlan& barrierPlan);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] bool ComputeOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] bool DecodedSrgbLastExecute() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
