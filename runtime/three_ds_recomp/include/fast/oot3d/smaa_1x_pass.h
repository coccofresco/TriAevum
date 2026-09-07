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

class Smaa1xPass final {
  public:
    Smaa1xPass();
    ~Smaa1xPass();
    Smaa1xPass(const Smaa1xPass&) = delete;
    Smaa1xPass& operator=(const Smaa1xPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 uint64_t frameId, VkImage colorImage,
                 VkFormat colorFormat,
                 SceneColorEncoding inputEncoding,
                 const EffectPassBarrierPlan& barrierPlan);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputsOwnedByNri() const;
    [[nodiscard]] bool LookupsOwnedByNri() const;
    [[nodiscard]] bool ComputeOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] SceneColorEncoding OutputEncoding() const;
    [[nodiscard]] bool LookupUploadOwnedByNri() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
