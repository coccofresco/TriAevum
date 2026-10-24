#pragma once
#include "fast/extension/pica_surface_coordinates.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/scene_view_runtime.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;

// Layering over a screen-space reflection already in the output (HiZ: RGB in
// the output's encoding, alpha = hit confidence x material weight). The
// resolved radiance then fills only the weight the screen-space hits leave
// uncovered: a = max(w, a_ss), rgb = (a_ss rgb_ss + (a - a_ss) rgb) / a.
struct ReflectionResolveLayering {
    bool FillScreenSpaceMisses = false;
    // The resolved term's weight relative to the screen-space strength.
    float FallbackScale = 1.0F;
    // The screen-space layer is sRGB encoded; the fill is encoded to match.
    bool OutputSrgb = false;
};

// Converts raw radiance (FidelityFX SSSR, Brixelizer GI specular) into the
// common reflection contract: RGB radiance plus a material-gated BRDF weight
// in alpha.
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
                 std::optional<PicaSurfaceCoordinates> coordinates = std::nullopt,
                 ReflectionResolveLayering layering = {});
    // Writes the configured output as no reflection (zero radiance and
    // weight), for a source with no data this sample: the declared output
    // stays complete and composes as nothing.
    bool ClearOutput(uint32_t frameIndex,
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
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
