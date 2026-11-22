#pragma once

#include <array>
#include <cstdint>

#if defined(ENABLE_OOT3D_VULKAN) || defined(ENABLE_RENDERER3DS_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace Fast::Renderer3ds {

// PICA200 texture wrap mode 1 samples the unit's RGBA8888 border color
// register (0x081 / 0x091 / 0x099) outside [0, 1].
inline constexpr uint8_t kPicaTextureWrapClampToBorder = 1U;

[[nodiscard]] constexpr bool PicaUsesBorderColor(
    uint8_t wrapS, uint8_t wrapT) noexcept {
    return wrapS == kPicaTextureWrapClampToBorder ||
           wrapT == kPicaTextureWrapClampToBorder;
}

// Mask out stale border color register contents when neither axis uses
// clamp-to-border so non-border draws keep identical keys and hashes.
[[nodiscard]] constexpr uint32_t EffectivePicaBorderColor(
    uint8_t wrapS, uint8_t wrapT, uint32_t rgba8) noexcept {
    return PicaUsesBorderColor(wrapS, wrapT) ? rgba8 : 0U;
}

[[nodiscard]] constexpr std::array<float, 4> DecodePicaBorderColorFloat(
    uint32_t rgba8) noexcept {
    return {
        static_cast<float>(rgba8 & 0xFFU) / 255.0F,
        static_cast<float>((rgba8 >> 8U) & 0xFFU) / 255.0F,
        static_cast<float>((rgba8 >> 16U) & 0xFFU) / 255.0F,
        static_cast<float>((rgba8 >> 24U) & 0xFFU) / 255.0F,
    };
}

// Nearest standard Vulkan/NRI float border color when VK_EXT_custom_border_color
// is unavailable on the device.
[[nodiscard]] constexpr std::array<float, 4> QuantizePicaBorderColorFloatFallback(
    uint32_t rgba8) noexcept {
    const uint32_t r = rgba8 & 0xFFU;
    const uint32_t g = (rgba8 >> 8U) & 0xFFU;
    const uint32_t b = (rgba8 >> 16U) & 0xFFU;
    const uint32_t a = (rgba8 >> 24U) & 0xFFU;
    if (a < 128U) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    if (r + g + b >= 384U) {
        return {1.0F, 1.0F, 1.0F, 1.0F};
    }
    return {0.0F, 0.0F, 0.0F, 1.0F};
}

#if defined(VK_VERSION_1_0)
// Configures VkSamplerCreateInfo::borderColor (and optionally chains
// customInfo into info.pNext). `customInfo` must remain alive until
// vkCreateSampler completes.
inline void ApplyVulkanBorderColor(
    VkSamplerCreateInfo& info,
    VkSamplerCustomBorderColorCreateInfoEXT& customInfo,
    uint32_t rgba8, bool integer, bool customBorderColorSupported) noexcept {
    if (info.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER &&
        info.addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
        info.borderColor = integer
            ? VK_BORDER_COLOR_INT_TRANSPARENT_BLACK
            : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        return;
    }
    if (integer) {
        if (rgba8 == 0U) {
            info.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
            return;
        }
        if (customBorderColorSupported) {
            info.borderColor = VK_BORDER_COLOR_INT_CUSTOM_EXT;
            customInfo = {
                VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
            customInfo.customBorderColor.uint32[0] = rgba8;
            customInfo.customBorderColor.uint32[1] = 0U;
            customInfo.customBorderColor.uint32[2] = 0U;
            customInfo.customBorderColor.uint32[3] = 0U;
            customInfo.format = VK_FORMAT_UNDEFINED;
            customInfo.pNext = info.pNext;
            info.pNext = &customInfo;
            return;
        }
        info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        return;
    }
    if (rgba8 == 0x00000000U) {
        info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        return;
    }
    if (rgba8 == 0xFF000000U) {
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        return;
    }
    if (rgba8 == 0xFFFFFFFFU) {
        info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        return;
    }
    if (customBorderColorSupported) {
        const auto rgba = DecodePicaBorderColorFloat(rgba8);
        info.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        customInfo = {
            VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT};
        customInfo.customBorderColor.float32[0] = rgba[0];
        customInfo.customBorderColor.float32[1] = rgba[1];
        customInfo.customBorderColor.float32[2] = rgba[2];
        customInfo.customBorderColor.float32[3] = rgba[3];
        customInfo.format = VK_FORMAT_UNDEFINED;
        customInfo.pNext = info.pNext;
        info.pNext = &customInfo;
        return;
    }
    const uint32_t r = rgba8 & 0xFFU;
    const uint32_t g = (rgba8 >> 8U) & 0xFFU;
    const uint32_t b = (rgba8 >> 16U) & 0xFFU;
    const uint32_t a = (rgba8 >> 24U) & 0xFFU;
    info.borderColor = a < 128U
        ? VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK
        : (r + g + b >= 384U ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                             : VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
}
#endif

} // namespace Fast::Renderer3ds
