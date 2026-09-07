#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaTextureUploadDesc {
    uint32_t FrameIndex = 0;
    uint64_t FrameId = 0;
    VkImage Image = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipLevel = 0;
    uint32_t MipLevels = 1;
    std::span<const uint8_t> Pixels;
};

class NriPicaTextureUploadPass final {
  public:
    NriPicaTextureUploadPass();
    ~NriPicaTextureUploadPass();
    NriPicaTextureUploadPass(
        const NriPicaTextureUploadPass&) = delete;
    NriPicaTextureUploadPass& operator=(
        const NriPicaTextureUploadPass&) = delete;

    bool Initialize(NriInteropContext& interop,
                    uint64_t bytesPerFrame);
    bool Execute(const NriPicaTextureUploadDesc& desc);
    void ForgetTexture(VkImage image);
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] uint64_t LastUploadedBytes() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
