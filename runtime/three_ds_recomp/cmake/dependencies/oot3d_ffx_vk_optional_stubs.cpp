#include <FidelityFX/host/backends/vk/ffx_vk.h>

// FidelityFX 1.1.4 wires the optional frame-generation swapchain callback
// into every Vulkan backend interface, even when only SSSR is built. Keep the
// focused renderer module independent from the much larger frame-interpolation
// swapchain implementation while returning the SDK's documented unsupported
// result if an unrelated caller ever tries to invoke it.
extern "C" FFX_API FfxErrorCode
ffxSetFrameGenerationConfigToSwapchainVK(
    const FfxFrameGenerationConfig*) {
    return FFX_ERROR_INVALID_ARGUMENT;
}
