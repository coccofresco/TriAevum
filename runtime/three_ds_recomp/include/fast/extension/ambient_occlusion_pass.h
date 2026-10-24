#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/graphics_settings.h"
#include "fast/extension/pica_surface_coordinates.h"
#include "fast/extension/scene_view_runtime.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Fast::Extension {
class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class AmbientOcclusionPass final {
  public:
    AmbientOcclusionPass();
    ~AmbientOcclusionPass();
    AmbientOcclusionPass(const AmbientOcclusionPass&) = delete;
    AmbientOcclusionPass& operator=(const AmbientOcclusionPass&) = delete;
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output, VkImage depth, VkFormat depthFormat,
                   VkImage normal, VkFormat normalFormat);
    bool Execute(uint32_t frameSlot, const PerspectiveViewState& view, bool wBuffer, const EffectsSettings& settings,
                 const EffectPassBarrierPlan& barriers, PicaSurfaceCoordinates coordinates);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Extension
#endif
