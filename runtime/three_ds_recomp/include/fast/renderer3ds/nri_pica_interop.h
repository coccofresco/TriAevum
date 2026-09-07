#pragma once

#ifdef ENABLE_RENDERER3DS_VULKAN

#include <vulkan/vulkan.h>

#ifdef ENABLE_RENDERER3DS_NRI
#include <NRI.h>
#endif

#include <cstdint>
#include <string>

namespace Fast::Renderer3ds {

// Narrow backend service boundary required by the reusable PICA/NRI executor.
// A title or host backend owns Vulkan/NRI interop lifetime and implements this
// interface; no gameplay or title policy crosses it.
class NriPicaInterop {
  public:
    virtual ~NriPicaInterop() = default;

    [[nodiscard]] virtual bool Available() const = 0;
    [[nodiscard]] virtual const std::string& UnavailableReason() const = 0;

#ifdef ENABLE_RENDERER3DS_NRI
    virtual bool WrapBuffer(VkBuffer buffer, uint64_t size,
                            uint8_t* mappedMemory) = 0;
    virtual bool WrapTexture(VkImage image, VkFormat format,
                             VkImageType type, VkImageUsageFlags usage,
                             uint32_t width, uint32_t height,
                             uint32_t mipLevels) = 0;
    [[nodiscard]] virtual nri::Device* Device() = 0;
    [[nodiscard]] virtual nri::CoreInterface* Core() = 0;
    [[nodiscard]] virtual nri::CommandBuffer* CommandBuffer(
        uint32_t frameIndex) = 0;
    [[nodiscard]] virtual nri::Descriptor* TextureView(
        VkImage image, bool storage) = 0;
    [[nodiscard]] virtual nri::Buffer* Buffer(VkBuffer buffer) = 0;
    [[nodiscard]] virtual nri::Pipeline* WrapGraphicsPipeline(
        VkPipeline pipeline) = 0;
    virtual void DestroyPipelineWrapper(nri::Pipeline* pipeline) = 0;
    virtual bool CmdSetPipeline(uint32_t frameIndex,
                                nri::Pipeline* pipeline) = 0;
#endif
};

} // namespace Fast::Renderer3ds

#endif
