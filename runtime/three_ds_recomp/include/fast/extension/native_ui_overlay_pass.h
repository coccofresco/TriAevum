#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/effect_graph_pass_barriers.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Fast::Extension {
class NriInteropContext;

// Lays a display's native UI over an upscaler's reconstruction of the scene
// before that UI; see native_ui_overlay.h. It runs inside the upscaler pass,
// and its output (RGBA16F at the upscaled extent, in the upscaled colour's
// encoding) is what the pass publishes as its upscaled colour.
class NativeUiOverlayPass final {
  public:
    struct Inputs {
        VkImage DisplayUi = VK_NULL_HANDLE;
        VkFormat DisplayUiFormat = VK_FORMAT_UNDEFINED;
        VkImage DisplayScene = VK_NULL_HANDLE;
        VkFormat DisplaySceneFormat = VK_FORMAT_UNDEFINED;
        uint32_t DisplayWidth = 0U;
        uint32_t DisplayHeight = 0U;
        VkImage Upscaled = VK_NULL_HANDLE;
        VkFormat UpscaledFormat = VK_FORMAT_UNDEFINED;
        uint32_t Width = 0U;
        uint32_t Height = 0U;
        bool LinearOutput = false;
    };

    NativeUiOverlayPass();
    ~NativeUiOverlayPass();
    NativeUiOverlayPass(const NativeUiOverlayPass&) = delete;
    NativeUiOverlayPass& operator=(const NativeUiOverlayPass&) = delete;
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    // Reads the inputs in their shader-read state and leaves the composed
    // colour readable.
    bool Execute(uint32_t frameSlot, uint64_t frameId, const Inputs& inputs);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Extension
#endif
