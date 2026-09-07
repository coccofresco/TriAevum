#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Fast::Oot3d {

struct PicaAlphaCoveragePolicy {
    bool AlphaTested = false;
    bool Enable = false;
};

[[nodiscard]] inline PicaAlphaCoveragePolicy
ResolvePicaAlphaCoveragePolicy(
    VkSampleCountFlagBits samples, bool alphaTestEnabled, bool depthTest,
    bool depthWrite, bool blending, uint8_t colorWriteMask) {
    PicaAlphaCoveragePolicy policy;
    policy.AlphaTested =
        alphaTestEnabled && depthTest && depthWrite && !blending &&
        (colorWriteMask & 0x7U) != 0U;
    policy.Enable =
        policy.AlphaTested && samples != VK_SAMPLE_COUNT_1_BIT;
    return policy;
}

} // namespace Fast::Oot3d
