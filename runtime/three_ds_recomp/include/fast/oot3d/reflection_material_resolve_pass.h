#pragma once
#include "fast/oot3d/pica_surface_coordinates.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/scene_view_runtime.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

// Converts raw FidelityFX radiance into the common reflection contract:
// RGB radiance plus a material-gated BRDF weight in alpha.
class ReflectionMaterialResolvePass final {
  public:
    ReflectionMaterialResolvePass();
    ~ReflectionMaterialResolvePass();
    ReflectionMaterialResolvePass(
        const ReflectionMaterialResolvePass&) = delete;
    ReflectionMaterialResolvePass& operator=(
        const ReflectionMaterialResolvePass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(const NriEffectGraphTransientImageBinding& output);
    bool Execute(uint32_t frameIndex,
                 VkImage reflectionImage, VkFormat reflectionFormat,
                 VkImage normalImage, VkFormat normalFormat,
                 VkImage materialImage, VkFormat materialFormat,
                 VkImage brdfImage, VkFormat brdfFormat,
                 const PerspectiveViewState& view,
                 float roughnessBias,
                 const EffectPassBarrierPlan& barrierPlan,
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt);
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
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
