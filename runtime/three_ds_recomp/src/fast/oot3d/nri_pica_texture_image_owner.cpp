#include "fast/oot3d/nri_pica_texture_image_owner.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#include <cstdlib>
#include <string_view>

namespace Fast::Oot3d {

bool NriPicaTextureImageOwner::Initialize(
    NriInteropContext& interop, const char* environmentVariable,
    const char* label) {
    Shutdown();
    const char* resolvedLabel =
        label != nullptr ? label : "NRI PICA image ownership";
    if (environmentVariable == nullptr ||
        environmentVariable[0] == '\0') {
        mReason = std::string(resolvedLabel) +
                  " environment policy is invalid";
        return false;
    }
    if (const char* enabled = std::getenv(environmentVariable);
        enabled != nullptr && std::string_view(enabled) == "0") {
        mReason = std::string(resolvedLabel) +
                  " disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mReason = interop.UnavailableReason();
        return false;
    }
    mInterop = &interop;
    mReason.clear();
    return true;
}

bool NriPicaTextureImageOwner::Create(
    uint32_t width, uint32_t height, VkFormat format,
    uint32_t mipLevels, NriPicaTextureImage& texture,
    bool createStorageView) {
    texture = {};
    if (!Available() || width == 0U || height == 0U || mipLevels == 0U ||
        format == VK_FORMAT_UNDEFINED)
        return false;
    NriOwnedTexture2D owned;
    if (!mInterop->CreateOwnedTexture2D(
            width, height, format, owned, mipLevels, createStorageView))
        return false;
    texture.Image = owned.Image;
    texture.View = owned.SampledView;
    texture.StorageView = owned.StorageView;
    if (texture.Image == VK_NULL_HANDLE ||
        texture.View == VK_NULL_HANDLE ||
        (createStorageView && texture.StorageView == VK_NULL_HANDLE)) {
        if (texture.Image != VK_NULL_HANDLE)
            mInterop->DestroyOwnedTexture(texture.Image);
        texture = {};
        return false;
    }
    return true;
}

void NriPicaTextureImageOwner::Destroy(VkImage image) {
    if (mInterop != nullptr && image != VK_NULL_HANDLE)
        mInterop->DestroyOwnedTexture(image);
}

void NriPicaTextureImageOwner::Shutdown() {
    mInterop = nullptr;
    mReason =
        "NRI PICA texture image ownership is not initialized";
}

bool NriPicaTextureImageOwner::Owns(VkImage image) const {
    return mInterop != nullptr && image != VK_NULL_HANDLE &&
           mInterop->OwnsTexture(image);
}

bool NriPicaTextureImageOwner::Available() const {
    return mInterop != nullptr && mReason.empty() &&
           mInterop->Available();
}

const std::string&
NriPicaTextureImageOwner::UnavailableReason() const {
    return mReason;
}

} // namespace Fast::Oot3d

#endif
