#pragma once
#include "fast/oot3d/pica_surface_coordinates.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/scene_view_runtime.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class HiZReflectionPass final {
  public:
    HiZReflectionPass();
    ~HiZReflectionPass();
    HiZReflectionPass(const HiZReflectionPass&) = delete;
    HiZReflectionPass& operator=(const HiZReflectionPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output,
                   VkImage colorImage, VkFormat colorFormat,
                   VkImage hiZImage, VkFormat hiZFormat,
                   VkImage normalImage, VkFormat normalFormat,
                   VkImage materialImage, VkFormat materialFormat);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 uint32_t hiZMipCount,
                 const PerspectiveViewState& view,
                 const EffectsSettings& settings,
                 const EffectPassBarrierPlan& barrierPlan,
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
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

} // namespace Fast::Oot3d

#endif
