#pragma once

#ifdef ENABLE_RENDERER3DS_VULKAN

#include "fast/renderer3ds/nri_pica_interop.h"
#include "fast/renderer3ds/pica_attachment_contract.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Fast::Renderer3ds {

struct NriPicaExecutionConfig {
    uint32_t FrameSlotCount = 2U;
    uint32_t MaxDrawsPerFrame = 512U;
    bool OwnedDraws = true;
    bool PreferOwnedUploads = false;
};

struct NriPicaGraphicsPipelineDesc {
    std::span<const uint32_t> VertexSpirv;
    std::span<const uint32_t> FragmentSpirv;
    std::span<const VkVertexInputBindingDescription> VertexBindings;
    std::span<const VkVertexInputAttributeDescription> VertexAttributes;
    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags CullMode = VK_CULL_MODE_NONE;
    VkFrontFace FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
    bool AlphaToCoverage = false;
    bool DepthTest = false;
    bool DepthWrite = false;
    VkCompareOp DepthCompare = VK_COMPARE_OP_ALWAYS;
    bool StencilTest = false;
    VkStencilOpState FrontStencil{};
    VkStencilOpState BackStencil{};
    std::array<VkPipelineColorBlendAttachmentState,
               kPicaColorAttachmentCount> Colors{};
    bool LogicOpEnabled = false;
    VkLogicOp LogicOp = VK_LOGIC_OP_COPY;
    std::array<VkFormat, kPicaColorAttachmentCount> ColorFormats{};
    uint32_t ColorAttachmentCount =
        static_cast<uint32_t>(kPicaColorAttachmentCount);
    VkFormat DepthStencilFormat = VK_FORMAT_UNDEFINED;
};

struct NriPicaTextureBindingDesc {
    VkImage Image = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipLevels = 1;
    VkFilter MinFilter = VK_FILTER_NEAREST;
    VkFilter MagFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VkSamplerAddressMode AddressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode AddressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float MipBias = 0.0F;
    float MinLod = 0.0F;
    float MaxLod = 0.0F;
    bool Integer = false;
};

struct NriPicaUniformBindingDesc {
    uint64_t Offset = 0;
    uint64_t Size = 0;
};

struct NriPicaVertexBufferBindingDesc {
    uint32_t Binding = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    uint32_t Stride = 0;
};

struct NriPicaOwnedDrawDesc {
    uint32_t FrameIndex = 0;
    uint64_t FrameId = 0;
    VkPipeline FallbackPipeline = VK_NULL_HANDLE;
    VkBuffer UniformBuffer = VK_NULL_HANDLE;
    uint64_t UniformBufferSize = 0;
    uint8_t* UniformMappedMemory = nullptr;
    // Vertex, fragment, previous-vertex and directional-shadow receiver UBOs.
    std::array<NriPicaUniformBindingDesc, 4> Uniforms{};
    VkBuffer VertexBuffer = VK_NULL_HANDLE;
    uint64_t VertexBufferSize = 0;
    uint8_t* VertexMappedMemory = nullptr;
    bool VertexContentPersistent = false;
    std::span<const NriPicaVertexBufferBindingDesc> VertexBindings;
    bool Indexed = false;
    uint64_t IndexOffset = 0;
    uint32_t VertexOrIndexCount = 0;
    int32_t BaseVertex = 0;
    std::array<NriPicaTextureBindingDesc, 3> Textures{};
    NriPicaTextureBindingDesc DirectionalShadowTexture;
    NriPicaTextureBindingDesc LightingLutTexture;
    VkImage StorageImage = VK_NULL_HANDLE;
    VkFormat StorageFormat = VK_FORMAT_UNDEFINED;
    uint32_t StorageWidth = 0;
    uint32_t StorageHeight = 0;
    std::array<float, 8> RootConstants{};
    VkViewport Viewport{};
    VkRect2D Scissor{};
    std::array<float, 4> BlendConstants{};
    bool StencilTest = false;
    uint8_t StencilReference = 0;
};

class NriPicaPipelineBridge final {
  public:
    NriPicaPipelineBridge();
    ~NriPicaPipelineBridge();
    NriPicaPipelineBridge(const NriPicaPipelineBridge&) = delete;
    NriPicaPipelineBridge& operator=(const NriPicaPipelineBridge&) = delete;

    bool Initialize(NriPicaInterop& interop,
                    const NriPicaExecutionConfig& config = {});
    bool InitializePipelineCache(std::span<const uint8_t> data = {});
    [[nodiscard]] std::vector<uint8_t> GetPipelineCacheData() const;
    bool CreateOwnedPipeline(
        VkPipeline fallbackPipeline,
        const NriPicaGraphicsPipelineDesc& desc);
    bool BindOwnedDraw(const NriPicaOwnedDrawDesc& desc);
    // Same layout/resources as the immediately preceding owned draw; no uploads.
    bool DrawBoundGeometry(const NriPicaOwnedDrawDesc& desc);
    bool Bind(uint32_t frameIndex, VkPipeline pipeline);
    void Forget(VkPipeline pipeline);
    void Reset();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool DescriptorLayoutOwnedByNri() const;
    [[nodiscard]] bool DescriptorsOwnedByNri() const;
    [[nodiscard]] bool OwnedDrawsEnabled() const;
    [[nodiscard]] bool LastDrawUploadsOwnedByNri() const;
    [[nodiscard]] uint64_t LastDrawUploadedBytes() const;
    [[nodiscard]] bool OwnedPipelineReady(VkPipeline fallbackPipeline) const;
    [[nodiscard]] size_t OwnedPipelineCount() const;
    [[nodiscard]] size_t WrappedPipelineCount() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Renderer3ds

#endif
