#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/scene_view_runtime.h"
#include "fast/oot3d/temporal_history_manager.h"
#include "fast/oot3d/pica_surface_coordinates.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

// Thin Vulkan adapter around the pinned FidelityFX SSSR host/backend. The
// effect owns its temporal internals; the final texture remains explicitly
// owned through NRI so the rest of the renderer keeps one resource contract.
class FidelityFxSssrPass final {
  public:
    FidelityFxSssrPass();
    ~FidelityFxSssrPass();
    FidelityFxSssrPass(const FidelityFxSssrPass&) = delete;
    FidelityFxSssrPass& operator=(const FidelityFxSssrPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool SetIblResources(VkImage environmentImage,
                         uint32_t environmentSize,
                         uint32_t environmentMipCount,
                         VkImage brdfImage, uint32_t brdfSize);
    bool Configure(uint32_t width, uint32_t height,
                   VkImage colorImage, VkFormat colorFormat,
                   VkImage depthImage, VkFormat depthFormat,
                   VkImage motionImage, VkFormat motionFormat,
                   VkImage normalImage, VkFormat normalFormat,
                   VkImage materialImage, VkFormat materialFormat);
    bool Execute(VkCommandBuffer commandBuffer,
                 const TemporalViewState& temporal,
                 const PerspectiveViewState& view,
                 const EffectsSettings& settings, uint32_t frameSlot,
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt);
    void InvalidateScreenResources();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] bool FidelityFxBackendActive() const;
    [[nodiscard]] bool IblResourcesConfigured() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
