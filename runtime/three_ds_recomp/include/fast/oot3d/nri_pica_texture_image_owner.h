#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaTextureImage {
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkImageView StorageView = VK_NULL_HANDLE;
};

class NriPicaTextureImageOwner final {
  public:
    bool Initialize(
        NriInteropContext& interop,
        const char* environmentVariable =
            "OOT3D_GRAPHICS_NRI_PICA_TEXTURE_IMAGES",
        const char* label = "NRI PICA texture image ownership");
    bool Create(uint32_t width, uint32_t height, VkFormat format,
                uint32_t mipLevels,
                NriPicaTextureImage& texture,
                bool createStorageView = false);
    void Destroy(VkImage image);
    void Shutdown();

    [[nodiscard]] bool Owns(VkImage image) const;
    [[nodiscard]] bool Available() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    NriInteropContext* mInterop = nullptr;
    std::string mReason =
        "NRI PICA texture image ownership is not initialized";
};

} // namespace Fast::Oot3d

#endif
