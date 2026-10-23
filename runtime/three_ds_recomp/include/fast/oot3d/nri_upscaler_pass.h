#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/pica_surface_coordinates.h"
#include "fast/oot3d/upscaler_orientation_pass.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;
struct NriEffectGraphTransientImageBinding;
struct NriTemporalUpscaleDispatchDesc;
enum class UpscalerProvider : uint8_t;
enum class UpscalerQuality : uint8_t;

class NriUpscalerPass final {
  public:
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    void Shutdown();
    bool Configure(const NriEffectGraphTransientImageBinding& output);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameIndex,
                 VkImage inputImage, VkFormat inputFormat,
                 uint32_t inputWidth, uint32_t inputHeight,
                 UpscalerQuality quality, float sharpness,
                 const EffectPassBarrierPlan& barrierPlan,
                 SceneColorEncoding inputEncoding);
    bool ExecuteFsr(VkCommandBuffer commandBuffer,
                    NriTemporalUpscaleDispatchDesc desc,
                    const EffectPassBarrierPlan& barrierPlan);
    // With `upright`, DLSS reconstructs the frame the way the screen shows it
    // (upscaler_orientation.h): its inputs are turned upright and its output
    // turned back onto the declared surface. Without it, or when the turn is
    // unavailable, it reconstructs the surface as stored.
    bool ExecuteDlss(VkCommandBuffer commandBuffer, NriTemporalUpscaleDispatchDesc desc,
                     const EffectPassBarrierPlan& barrierPlan,
                     std::optional<PicaSurfaceCoordinates> upright = std::nullopt);
    // Whether the last DLSS dispatch reconstructed upright.
    [[nodiscard]] bool LastDispatchUpright() const;
    void InvalidateScreenResources();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool FsrAvailable() const;
    [[nodiscard]] bool DlssAvailable() const;
    [[nodiscard]] const std::string& UnavailableReason() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] uint32_t OutputWidth() const;
    [[nodiscard]] uint32_t OutputHeight() const;
    [[nodiscard]] bool OutputOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] SceneColorEncoding OutputEncoding() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;

  private:
    bool ExecuteTemporal(VkCommandBuffer commandBuffer, NriTemporalUpscaleDispatchDesc desc, UpscalerProvider provider,
                         const EffectPassBarrierPlan& barrierPlan, std::optional<PicaSurfaceCoordinates> upright);
    void ReleaseOutput();
    bool BeginOutput(uint32_t frameIndex,
                     const EffectPassOutputBarrier& outputBarrier);
    bool EndOutput(uint32_t frameIndex,
                   const EffectPassOutputBarrier& outputBarrier);

    VkDevice mDevice = VK_NULL_HANDLE;
    NriInteropContext* mInterop = nullptr;
    VkImage mOutputImage = VK_NULL_HANDLE;
    VkImageView mOutputView = VK_NULL_HANDLE;
    uint32_t mOutputWidth = 0;
    uint32_t mOutputHeight = 0;
    bool mNriBarriersUsed = false;
    EffectPassBarrierExecution mLastBarrierExecution;
    ResourceStateTracker* mOutputStates = nullptr;
    SceneColorEncoding mOutputEncoding = SceneColorEncoding::Unknown;
    UpscalerOrientationPass mOrientation;
    bool mLastDispatchUpright = false;
    std::string mUnavailableReason = "NRI upscaler is not initialized";
};

} // namespace Fast::Oot3d

#endif
