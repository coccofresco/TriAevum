#pragma once
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/linear_scene_color.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
namespace Fast::Oot3d {
class NriInteropContext;
class TemporalAaPass final {
  public:
    TemporalAaPass();
    ~TemporalAaPass();
    bool Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    NriInteropContext& interop);
    bool Configure(uint32_t width, uint32_t height);
    bool Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot,
                 uint64_t renderedFrameId,
                 VkImage currentColor, VkFormat currentColorFormat,
                 VkImage motionSurface, VkFormat motionFormat,
                 bool historyValid,
                 float historyWeight, float clampExpansion, float sharpness,
                 SceneColorEncoding inputEncoding,
                 const EffectPassBarrierPlan& barrierPlan);
    void ResetHistory();
    void InvalidateScreenResources();
    void Shutdown();
    [[nodiscard]] bool Available() const;
    [[nodiscard]] VkImage OutputImage() const;
    [[nodiscard]] std::array<VkImage, 2> OutputImages() const;
    [[nodiscard]] VkImageView OutputView() const;
    [[nodiscard]] bool HistoryUsedLastExecute() const;
    [[nodiscard]] bool HistoryOwnedByNri() const;
    [[nodiscard]] bool ComputeOwnedByNri() const;
    [[nodiscard]] bool BarriersOwnedByNri() const;
    [[nodiscard]] SceneColorEncoding OutputEncoding() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
} // namespace Fast::Oot3d
#endif
