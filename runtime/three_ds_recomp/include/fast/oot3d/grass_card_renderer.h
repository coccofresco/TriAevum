#pragma once

#ifdef ENABLE_OOT3D_VULKAN

// The GPU side of the distant grass cards (grass_card_lod.h): the card atlas
// image, the card record buffers, the descriptor sets and the six pipelines
// of the native targets. Owned and driven by InteractiveGrassPass, which
// shares its environment, native lighting and shadow grid buffers with it.
// docs/TRIAEVUM_GRASS_CARD_LOD.md.

#include "fast/oot3d/grass_card_atlas.h"
#include "fast/oot3d/grass_card_lod.h"
#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/grass_pipeline_factory.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Fast::Oot3d {

// The frame's buffers the cards share with the blades.
struct GrassCardFrameInputs {
    VkDescriptorBufferInfo Environment{};
    VkDescriptorImageInfo NativeLighting{};
    // The coarse shadow grid and the actor patches, or any valid buffer when
    // the placements receive no shadows (the records then hold no points).
    VkDescriptorBufferInfo ShadowVisibility{};
    VkDescriptorBufferInfo ShadowPatches{};
};

// One level of one placement: Flags[1] is the level, Flags[3] the
// environment record, as the card shaders read them.
struct GrassCardDrawBatch {
    GrassPushConstants Push{};
    uint32_t FirstCard = 0U;
    uint32_t CardCount = 0U;
};

struct GrassCardPrepareResult {
    uint64_t StaticUploadedBytes = 0U;
    uint64_t DynamicUploadedBytes = 0U;
    bool AtlasRebuilt = false;
};

class GrassCardRenderer final {
  public:
    GrassCardRenderer();
    ~GrassCardRenderer();
    GrassCardRenderer(const GrassCardRenderer&) = delete;
    GrassCardRenderer& operator=(const GrassCardRenderer&) = delete;

    // Compiles the card programs and creates the pipelines for the targets.
    // False (with UnavailableReason) leaves the cards off; the blades draw.
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, Renderer::CachedPassShaderCompiler& shaders,
                    const GrassPipelineTargets& targets);
    void Shutdown();
    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] const std::string& UnavailableReason() const noexcept;

    // Records this frame's uploads outside a render pass: the atlas when the
    // shape changed (built on the calling thread), a new record buffer when
    // their revision changed (the previous one retires after the frames in
    // flight, so recorded commands keep theirs); then writes the invocation's
    // descriptor set. Throws GrassGpuOutOfMemory or std::runtime_error like
    // the blade buffers; the caller then drops the cards and keeps the blades.
    GrassCardPrepareResult Prepare(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId,
                                   const GrassCardAtlasShape& shape, std::span<const GrassCardRecord> records,
                                   uint64_t recordsRevision, const GrassCardFrameInputs& inputs);

    // Draws the batches inside the native render pass with the pipeline of
    // the attachment set (0 canonical, 1 instrumented, 2 with the albedo
    // guide). The fragments write their texel's blade depth; batches whose
    // PICA depth is the raster depth take the conservative-depth variant
    // (depth_greater, early depth test kept). Needs a Prepare in this frame.
    void Draw(VkCommandBuffer commandBuffer, uint32_t width, uint32_t height, size_t pipelineIndex,
              std::span<const GrassCardDrawBatch> batches);

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
