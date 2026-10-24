#pragma once

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/graphics_settings.h"
#include "fast/extension/linear_scene_color.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

class SceneCompositePass final {
  public:
    SceneCompositePass();
    ~SceneCompositePass();
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId, VkImage sceneColor, VkFormat sceneColorFormat,
                 VkImage aoVisibility, VkFormat aoFormat, VkImage reflection, VkFormat reflectionFormat,
                 VkImage materialGuide, VkFormat materialGuideFormat, VkImage normalGuide, VkFormat normalGuideFormat,
                 VkImage transparentDepthGuide, VkFormat transparentDepthGuideFormat, VkImage ambientGuide,
                 VkFormat ambientGuideFormat, VkImage depthGuide, VkFormat depthGuideFormat, VkImage fogGuide,
                 VkFormat fogGuideFormat, VkImage outlineGeometryGuide, VkFormat outlineGeometryGuideFormat,
                 VkImage diffuseIllumination, VkFormat diffuseIlluminationFormat, VkImage diffuseAlbedoGuide,
                 VkFormat diffuseAlbedoGuideFormat, bool globalIlluminationEnabled, float diffuseIntensity,
                 bool aoEnabled, uint32_t aoDebugView, bool reflectionsEnabled, float reflectionStrength, uint32_t reflectionDebug,
                 bool outlineEnabled, const ToonStyleSettings& outlineSettings, SceneColorEncoding inputEncoding,
                 const EffectPassBarrierPlan& barrierPlan);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] bool ComputeOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] SceneColorEncoding OutputEncoding() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension
#endif
