#pragma once

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/effect_geometry_provider_plan.h"
#include "fast/oot3d/grass_world_placement_cache.h"
#include "fast/oot3d/grass_shadow_refinement.h"
#include "fast/oot3d/nri_directional_shadow_pass.h"
#include "fast/renderer/shaderc_compiler.h"
#include <memory>
#include <span>
#include <vulkan/vulkan.h>

namespace Fast::Oot3d {
struct GrassShadowGridResult {
    VkDescriptorBufferInfo Visibility{};
    VkDescriptorBufferInfo References{};
    VkDescriptorBufferInfo Patches{};
    uint32_t PointCount = 0;
    uint32_t RefinementPointCount = 0;
    uint32_t RefinementPatchCount = 0;
    float RefinementCellSize = 0;
    uint64_t StaticUploadedBytes = 0;
    uint64_t DynamicUploadedBytes = 0;
};

// Private GPU producer within the graph-authorized Grass invocation. It reads
// the published atlas/history pair and never changes VSM residency or casters.
// The coarse grid (the ground textures' 4x4 lattice) reads the static
// casters' depth: Link and the actors shadow the grass only through the
// refinement around them, blade by blade.
class GrassShadowGridPass final {
  public:
    GrassShadowGridPass();
    ~GrassShadowGridPass();
    GrassShadowGridPass(const GrassShadowGridPass&) = delete;
    GrassShadowGridPass& operator=(const GrassShadowGridPass&) = delete;
    void Initialize(VkPhysicalDevice physical, VkDevice device, Renderer::CachedPassShaderCompiler& shaders,
                    VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    [[nodiscard]] bool Available() const noexcept;
    GrassShadowGridResult Execute(VkCommandBuffer command, uint32_t frameSlot, uint64_t frameId, uint64_t revision,
                                  std::span<const GrassShadowGridPoint> points,
                                  std::span<const GrassShadowGridReference> references,
                                  std::span<const GrassShadowSurface> surfaces,
                                  const std::array<Renderer3ds::ShadowBounds, 2>& focus,
                                  const EffectGeometryProviderPlan& plan,
                                  const NriDirectionalShadowHistorySnapshot& history);
    void Shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Oot3d
#endif
