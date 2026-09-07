#pragma once

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/effect_graph_pass_barriers.h"
#include "fast/oot3d/pica_scanout_effects.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Fast::Oot3d {

class NriInteropContext;

struct NriPicaScanoutDesc {
    uint32_t FrameSlot = 0;
    uint64_t FrameId = 0;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkImage TargetImage = VK_NULL_HANDLE;
    VkImageView TargetView = VK_NULL_HANDLE;
    VkFormat TargetFormat = VK_FORMAT_UNDEFINED;
    uint32_t TargetWidth = 0;
    uint32_t TargetHeight = 0;
    std::array<VkImage, 11> InputImages{};
    std::array<VkFormat, 11> InputFormats{};
    PicaScanoutPushConstants Push{};
    float ViewportX = 0.0F;
    float ViewportY = 0.0F;
    float ViewportWidth = 0.0F;
    float ViewportHeight = 0.0F;
    int32_t ScissorX = 0;
    int32_t ScissorY = 0;
    uint32_t ScissorWidth = 0;
    uint32_t ScissorHeight = 0;
    std::array<float, 4> ClearColor{};
    bool Overlay = false;
};

class NriPicaScanoutPass final {
  public:
    NriPicaScanoutPass();
    ~NriPicaScanoutPass();
    NriPicaScanoutPass(const NriPicaScanoutPass&) = delete;
    NriPicaScanoutPass& operator=(const NriPicaScanoutPass&) = delete;

    bool Initialize(VkDevice device, NriInteropContext& interop);
    bool Configure(VkFormat targetFormat);
    bool Execute(const NriPicaScanoutDesc& desc,
                 const EffectPassBarrierPlan& barrierPlan);
    void ResetTargets();
    void Shutdown();

    [[nodiscard]] bool Available() const;
    [[nodiscard]] bool PipelineOwnedByNri() const;
    [[nodiscard]] bool DescriptorsOwnedByNri() const;
    [[nodiscard]] const EffectPassBarrierExecution& LastBarrierExecution()
        const;
    [[nodiscard]] const std::string& UnavailableReason() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Fast::Oot3d

#endif
