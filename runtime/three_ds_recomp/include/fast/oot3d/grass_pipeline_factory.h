#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/pica_attachment_contract.h"
#include "fast/renderer/shaderc_compiler.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Fast::Oot3d {

// A pass program from the cached compiler (the built-in pack, or a compile),
// with one optional define. Throws std::runtime_error.
[[nodiscard]] VkShaderModule CompileGrassShaderModule(Renderer::CachedPassShaderCompiler& shaders, VkDevice device,
                                                      const char* source, Renderer::SpirvStage stage,
                                                      const char* name, const char* defineName = nullptr,
                                                      const char* defineValue = nullptr);

// Where the grass draws render: the native PICA targets (canonical colour
// only, instrumented guides, instrumented with the GI albedo guide), under a
// render pass or dynamic rendering.
struct GrassPipelineTargets {
    VkRenderPass CanonicalRenderPass = VK_NULL_HANDLE;
    VkRenderPass InstrumentedRenderPass = VK_NULL_HANDLE;
    VkSampleCountFlagBits SampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool DynamicRendering = false;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkPipelineCache PipelineCache = VK_NULL_HANDLE;
};

// Groups of three pipelines, each canonical, instrumented, instrumented with
// the albedo guide (dynamic rendering only; VK_NULL_HANDLE otherwise):
// [0..2] explicit PICA depth, [3..5] rasterized depth, and for the blades
// [6..8] rasterized depth without discard (GrassBladePipelineSet).
using GrassPipelineSet = std::array<VkPipeline, 6>;
using GrassBladePipelineSet = std::array<VkPipeline, 9>;

// Fragments: a canonical and an instrumented program per group of three
// pipelines (explicit depth, raster depth, ...). No vertex input (the shaders
// read storage buffers), triangle list, no culling, depth test and write (less
// or equal), the grass blend and write masks of every attachment. The
// instrumented pipelines target the guides `instrumented` binds (dynamic
// rendering; the render pass binds every base guide). Throws
// std::runtime_error on failure after destroying what it created.
void CreateGrassPipelineSet(VkDevice device, const GrassPipelineTargets& targets, VkPipelineLayout layout,
                            VkShaderModule vertex, std::span<const VkShaderModule> fragments,
                            std::span<VkPipeline> pipelines,
                            PicaAttachmentRequirements instrumented = {kAllPicaAuxiliaryOutputs});

void DestroyGrassPipelineSet(VkDevice device, std::span<VkPipeline> pipelines) noexcept;

// A grass pipeline set per bound guide layout: the every-guide set at once,
// the others the first time a frame binds them. Owns its shader modules.
class GrassPipelineVariants final {
  public:
    GrassPipelineVariants() = default;
    ~GrassPipelineVariants();
    GrassPipelineVariants(const GrassPipelineVariants&) = delete;
    GrassPipelineVariants& operator=(const GrassPipelineVariants&) = delete;

    // Takes the modules (also on failure). Throws std::runtime_error.
    void Initialize(VkDevice device, const GrassPipelineTargets& targets, VkPipelineLayout layout,
                    VkShaderModule vertex, std::span<const VkShaderModule> fragments, size_t pipelineCount);
    // The set for a frame's attachments, indexed like CreateGrassPipelineSet.
    // Throws std::runtime_error when its pipelines cannot be created.
    [[nodiscard]] std::span<const VkPipeline> For(const PicaAttachmentRequirements& attachments);
    // The every-guide set exists.
    [[nodiscard]] bool Available() const noexcept;
    void Destroy() noexcept;

  private:
    VkDevice mDevice = VK_NULL_HANDLE;
    GrassPipelineTargets mTargets;
    VkPipelineLayout mLayout = VK_NULL_HANDLE;
    VkShaderModule mVertex = VK_NULL_HANDLE;
    std::vector<VkShaderModule> mFragments;
    size_t mPipelineCount = 0;
    std::unordered_map<uint8_t, std::vector<VkPipeline>> mSets;
};

} // namespace Fast::Oot3d

#endif
