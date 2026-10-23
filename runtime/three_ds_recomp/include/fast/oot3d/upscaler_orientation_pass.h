#pragma once
#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/pica_surface_coordinates.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Fast::Oot3d {
class NriInteropContext;

// Turns a temporal upscaler's inputs upright and its output back onto the
// surface (upscaler_orientation.h). It runs inside the upscaler pass; its
// images are private, read and written in the same frame.
class UpscalerOrientationPass final {
  public:
    struct Inputs {
        VkImage Color = VK_NULL_HANDLE;
        VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
        VkImage Depth = VK_NULL_HANDLE;
        VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
        VkImage Motion = VK_NULL_HANDLE;
        VkFormat MotionFormat = VK_FORMAT_UNDEFINED;
        VkImage Reactive = VK_NULL_HANDLE;
        VkFormat ReactiveFormat = VK_FORMAT_UNDEFINED;
        // The surface extent of all four.
        uint32_t Width = 0U;
        uint32_t Height = 0U;
    };

    UpscalerOrientationPass();
    ~UpscalerOrientationPass();
    UpscalerOrientationPass(const UpscalerOrientationPass&) = delete;
    UpscalerOrientationPass& operator=(const UpscalerOrientationPass&) = delete;
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop);
    // Reads the inputs in their shader-read state and leaves the upright
    // images (RGBA16F colour, R32F depth, RGBA16F motion, R16F reactive)
    // readable. A colour format whose texels are not its values (sRGB) is
    // refused.
    bool TurnInputs(uint32_t frameSlot, const Inputs& inputs, PicaSurfaceCoordinates coordinates,
                    EffectPassBarrierExecution& barriers);
    // The upright output of a surface of `width` x `height`, left writable as a
    // storage image for the upscaler.
    bool BeginOutput(uint32_t frameSlot, uint32_t width, uint32_t height, EffectPassBarrierExecution& barriers);
    // Writes the upright output onto `target` (RGBA16F, `width` x `height`,
    // already in its storage-write state).
    bool TurnOutput(uint32_t frameSlot, VkImage target, uint32_t width, uint32_t height,
                    PicaSurfaceCoordinates coordinates, EffectPassBarrierExecution& barriers);
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage ColorImage() const;
    [[nodiscard]] VkImage DepthImage() const;
    [[nodiscard]] VkImage MotionImage() const;
    [[nodiscard]] VkImage ReactiveImage() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Oot3d
#endif
