#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include "fast/extension/graphics_settings.h"
#include "fast/extension/pica_surface_coordinates.h"
#include "fast/extension/scene_view_runtime.h"
#include "fast/extension/temporal_history_manager.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Extension {

class BrixelizerScene;
class NriInteropContext;
struct BrixelizerGiInstance;
struct NriEffectGraphTransientImageBinding;

// Declared reads of the GlobalIllumination display pass, at the scene
// surface's extent.
struct BrixelizerGiInputs {
    uint32_t Width = 0;
    uint32_t Height = 0;
    // Packed surface guide: its direct radiance (low bytes, sRGB) is the lit
    // history, the only light the trace bounces.
    VkImage SurfaceGuide = VK_NULL_HANDLE;
    VkFormat SurfaceGuideFormat = VK_FORMAT_UNDEFINED;
    VkImage Depth = VK_NULL_HANDLE; // native perspective depth (D3D [0, 1])
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkImage NormalGuide = VK_NULL_HANDLE; // view-space normal, 0.5 n + 0.5
    VkFormat NormalGuideFormat = VK_FORMAT_UNDEFINED;
    VkImage MaterialGuide = VK_NULL_HANDLE; // G: perceptual roughness
    VkFormat MaterialGuideFormat = VK_FORMAT_UNDEFINED;
    VkImage Motion = VK_NULL_HANDLE;
    VkFormat MotionFormat = VK_FORMAT_UNDEFINED;
    bool operator==(const BrixelizerGiInputs&) const = default;
};

// Lifetime counters (totals since initialization).
struct BrixelizerGiStats {
    uint64_t InstancesCreated = 0;
    uint64_t InstancesRetired = 0; // resolution or specular change, or least recently used
    uint64_t WorldResets = 0;
    uint64_t SourceResets = 0; // RestartsGlobalIlluminationHistory
    uint64_t CreationFailures = 0; // instances whose creation failed (retried later)
};

// Global illumination traced through the scene distance field (FidelityFX
// Brixelizer GI) into the graph's DiffuseIllumination image and, when
// requested, its SpecularIllumination image (a reflection source).
//
// Each composed scene surface (presentation target, native boundary source)
// has its own instance: GI context, prepared inputs and their one-frame
// histories (depth, world normals, linear lit colour). Several surfaces can
// be composed within one command buffer, so an instance used this frame is
// never retired. Nothing here changes native targets.
class BrixelizerGiPass final {
  public:
    BrixelizerGiPass();
    ~BrixelizerGiPass();
    BrixelizerGiPass(const BrixelizerGiPass&) = delete;
    BrixelizerGiPass& operator=(const BrixelizerGiPass&) = delete;

    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop, BrixelizerScene& scene);
    // Selects (or creates) the instance of these inputs for frame `frameId`.
    // Linear radiance of the uniform environment rays escape to (the native
    // ambient light: open surfaces then receive what native ambient gives).
    // `specular` traces the specular term as well (another context).
    // `sourceWeights` are the weights the surface guide's direct radiance
    // carries (GlobalIlluminationSourceMix). The radiance cache averages its
    // samples over seconds (up to 64 per brick, more in coarser cascades):
    // when RestartsGlobalIlluminationHistory the instance starts over,
    // otherwise the new weights blend in. After a failed creation no instance
    // is created for kCreationRetryFrames frames (each attempt allocates the
    // surface's images and the SDK's pipelines and resources).
    static constexpr uint64_t kCreationRetryFrames = 120;
    bool Configure(uint64_t frameId, const BrixelizerGiInputs& inputs,
                   GlobalIlluminationResolution resolution, bool specular,
                   const std::array<float, 4>& sourceWeights);
    // `specularOutput` is the declared SpecularIllumination image, or null
    // when the pass does not declare it.
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId,
                 const TemporalViewState& temporal, const PerspectiveViewState& view,
                 PicaSurfaceCoordinates coordinates,
                 const NriEffectGraphTransientImageBinding& output,
                 const NriEffectGraphTransientImageBinding* specularOutput,
                 // Linear radiance rays receive when they leave the scene (the
                 // sky light at its source weight).
                 const std::array<float, 3>& environment,
                 const EffectPassBarrierPlan& barriers);
    // No GI for this sample (the field has no data yet, W-buffer depth, a
    // failed dispatch): the declared outputs hold zero radiance, so the
    // composite and the reflection resolve add nothing and the graph stays
    // complete.
    bool ClearOutput(uint32_t frameSlot, const NriEffectGraphTransientImageBinding& output,
                     const NriEffectGraphTransientImageBinding* specularOutput,
                     const EffectPassBarrierPlan& barriers);
    // Callers have completed the GPU work that references the instances.
    void InvalidateScreenResources();
    // The world changed (another scene, GI disabled): completes the recorded
    // work and drops every surface instance, so no screen probe, radiance
    // cache or history of the previous world survives. Camera cuts and
    // continuity breaks inside one world keep them; the GI context rejects
    // history by world position.
    void ResetHistory();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] const BrixelizerGiStats& Stats() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    template <typename Transition>
    bool Trace(BrixelizerGiInstance& instance, VkCommandBuffer commandBuffer, uint32_t frameSlot,
               uint64_t frameId, const TemporalViewState& temporal, const PerspectiveViewState& view,
               PicaSurfaceCoordinates coordinates, const std::array<float, 3>& environment,
               const Transition& privateTransition);

    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Extension

#endif
